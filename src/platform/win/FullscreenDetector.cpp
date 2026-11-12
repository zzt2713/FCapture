#include "platform/win/FullscreenDetector.h"

#include <windows.h>
#include <dwmapi.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

#include <vector>

namespace {

// 坐标允许 1px 误差（DWM 扩展边框与显示器边界偶尔差一像素）
bool coordinatesMatch(LONG lhs, LONG rhs)
{
    const LONG diff = lhs - rhs;
    return diff >= -1 && diff <= 1;
}

// 前台窗口是否属于**其他进程**（排除自己：截图遮罩、贴图窗等）
bool isExternalProcess(DWORD processId)
{
    return processId != 0 && processId != ::GetCurrentProcessId();
}

// ---- 游戏判定（对齐 snow 的 gamewindowclassification.cpp）----

QString normalizedWindowsPath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.startsWith(QStringLiteral("//?/UNC/"), Qt::CaseInsensitive))
        path = QStringLiteral("//") + path.mid(8);
    else if (path.startsWith(QStringLiteral("//?/")))
        path.remove(0, 4);
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

bool sameWindowsPath(const QString &first, const QString &second)
{
    return normalizedWindowsPath(first).compare(normalizedWindowsPath(second),
                                                Qt::CaseInsensitive)
           == 0;
}

// 引擎编辑器不是游戏：Unity.exe / UnrealEditor* / UE4Editor*
bool isEditorExecutable(const QString &name)
{
    return name.compare(QStringLiteral("Unity.exe"), Qt::CaseInsensitive) == 0
           || name.startsWith(QStringLiteral("UnrealEditor"), Qt::CaseInsensitive)
           || name.startsWith(QStringLiteral("UE4Editor"), Qt::CaseInsensitive);
}

// GameConfigStore 用「小写文件名的 UTF-16LE 字节」做 SHA1 作为父键。
QString gameConfigStoreParentKey(const QString &executablePath)
{
    const QString name = QFileInfo(normalizedWindowsPath(executablePath)).fileName().toLower();
    if (name.isEmpty())
        return {};

    QByteArray utf16;
    utf16.reserve(name.size() * 2);
    for (QChar character : name) {
        const ushort value = character.unicode();
        utf16.append(char(value & 0xff));
        utf16.append(char(value >> 8));
    }
    return QString::fromLatin1(QCryptographicHash::hash(utf16, QCryptographicHash::Sha1).toHex());
}

QStringList registryStrings(HKEY key, const wchar_t *valueName, DWORD type)
{
    DWORD bytes = 0;
    constexpr DWORD kMaxBytes = 128 * 1024;
    if (::RegGetValueW(key, nullptr, valueName, type, nullptr, nullptr, &bytes) != ERROR_SUCCESS
        || bytes == 0 || bytes > kMaxBytes || bytes % sizeof(wchar_t) != 0)
        return {};

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    if (::RegGetValueW(key, nullptr, valueName, type, nullptr, buffer.data(), &bytes)
        != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buffer.data(), qsizetype(bytes / sizeof(wchar_t)))
        .split(QChar(u'\0'), Qt::SkipEmptyParts);
}

// 读 HKCU 下记录的游戏 exe 全路径（GameDVR 的 GameConfigStore，只读非公开 API）。
QStringList registeredGamePaths(const QString &parentKey)
{
    if (parentKey.isEmpty())
        return {};

    QStringList paths;
    const QString parentPath = QStringLiteral("System\\GameConfigStore\\Parents\\") + parentKey;
    HKEY parent = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                        reinterpret_cast<const wchar_t *>(parentPath.utf16()), 0,
                        KEY_QUERY_VALUE, &parent) != ERROR_SUCCESS)
        return paths;

    const QStringList children = registryStrings(parent, L"Children", RRF_RT_REG_MULTI_SZ);
    ::RegCloseKey(parent);

    for (const QString &child : children) {
        if (child.trimmed().isEmpty())
            continue;
        const QString childPath = QStringLiteral("System\\GameConfigStore\\Children\\") + child;
        HKEY record = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER,
                            reinterpret_cast<const wchar_t *>(childPath.utf16()), 0,
                            KEY_QUERY_VALUE, &record) != ERROR_SUCCESS)
            continue;

        const QStringList matched =
            registryStrings(record, L"MatchedExeFullPath", RRF_RT_REG_SZ);
        ::RegCloseKey(record);
        if (!matched.isEmpty())
            paths.append(matched.first());
    }
    return paths;
}

// 目录下的 MicrosoftGame.config 是否声明了这个 exe（MSIX 打包游戏）
bool gameConfigDeclaresExecutable(const QDir &directory, const QString &executablePath)
{
    QFile file(directory.filePath(QStringLiteral("MicrosoftGame.config")));
    constexpr qint64 kMaxConfigBytes = 1024 * 1024;
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxConfigBytes)
        return false;

    QXmlStreamReader xml(file.read(kMaxConfigBytes + 1));
    QStringList elements;
    bool matches = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            elements.append(xml.name().toString());
            if (elements
                == QStringList{ QStringLiteral("Game"), QStringLiteral("ExecutableList"),
                                QStringLiteral("Executable") }) {
                const QString name = normalizedWindowsPath(
                    xml.attributes().value(QStringLiteral("Name")).toString());
                if (!name.isEmpty() && sameWindowsPath(directory.filePath(name), executablePath))
                    matches = true;
            }
        } else if (xml.isEndElement() && !elements.isEmpty()) {
            elements.removeLast();
        }
    }
    return !xml.hasError() && matches;
}

bool isGameWindow(HWND window, DWORD processId)
{
    const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;

    wchar_t exePath[32768] = {};
    DWORD pathSize = DWORD(sizeof(exePath) / sizeof(wchar_t));
    const bool gotPath = ::QueryFullProcessImageNameW(process, 0, exePath, &pathSize) != FALSE;
    ::CloseHandle(process);
    if (!gotPath)
        return false;

    wchar_t windowClass[256] = {};
    const int classSize = ::GetClassNameW(window, windowClass, 256);

    const QString exe = normalizedWindowsPath(QString::fromWCharArray(exePath, int(pathSize)));
    const QFileInfo executable(exe);
    const QString name = executable.fileName();
    if (name.isEmpty() || !name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        || isEditorExecutable(name))
        return false;

    // 1) GameDVR 注册表里登记过的 exe（Steam/Xbox 收录的游戏）
    const QStringList registered = registeredGamePaths(gameConfigStoreParentKey(exe));
    for (const QString &path : registered) {
        if (sameWindowsPath(exe, path))
            return true;
    }

    // 2) 目录（含最多 8 层祖先）下的 MicrosoftGame.config 声明了这个 exe
    const QDir executableDirectory = executable.dir();
    QDir directory = executableDirectory;
    for (int ancestor = 0; ancestor <= 8; ++ancestor) {
        if (gameConfigDeclaresExecutable(directory, exe))
            return true;
        if (!directory.cdUp())
            break;
    }

    const QString cls = QString::fromWCharArray(windowClass, classSize);

    // 3) Unity：UnityWndClass + UnityPlayer.dll + <名字>_Data 目录
    if (cls == QStringLiteral("UnityWndClass")) {
        return QFileInfo(executableDirectory.filePath(QStringLiteral("UnityPlayer.dll"))).isFile()
               && QFileInfo(executableDirectory.filePath(executable.completeBaseName()
                                                         + QStringLiteral("_Data")))
                      .isDir();
    }

    // 4) Unreal：UnrealWindow + *-Win64-Shipping.exe 之类的打包产物
    if (cls == QStringLiteral("UnrealWindow")) {
        for (const auto *suffix : { "-Win64-Shipping.exe", "-Win32-Shipping.exe",
                                    "-WinGDK-Shipping.exe" }) {
            if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive))
                return true;
        }
    }

    // 5) 少量常见游戏窗口类兜底（无引擎标记的老游戏）
    static const QStringList kGameClasses = {
        QStringLiteral("valve001"),
        QStringLiteral("sdldraw"),
    };
    return kGameClasses.contains(cls.toLower());
}

bool matchesFullscreen(HWND foreground, bool gamesOnly)
{
    if (!foreground || !::IsWindow(foreground) || !::IsWindowVisible(foreground)
        || ::IsIconic(foreground))
        return false;

    // 归到根窗口（子窗口不是"全屏应用"）
    HWND window = foreground;
    if (HWND root = ::GetAncestor(foreground, GA_ROOT); root)
        window = root;

    if (window == ::GetDesktopWindow() || window == ::GetShellWindow())
        return false;

    DWORD processId = 0;
    if (::GetWindowThreadProcessId(window, &processId) == 0 || !isExternalProcess(processId))
        return false;

    // UWP 幽灵窗 / 虚拟桌面隐藏窗：不是真正的前台全屏
    DWORD cloaked = 0;
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))
        || cloaked != 0)
        return false;

    // 用 DWM 扩展边框（不含 Win10/11 约 8px 的不可见缩放边框）
    constexpr DWORD kExtendedFrameBounds = 9;
    RECT frame{};
    if (FAILED(::DwmGetWindowAttribute(window, kExtendedFrameBounds, &frame, sizeof(frame))))
        return false;

    const HMONITOR monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    if (!monitor)
        return false;

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)))
        return false;

    // 窗口外沿必须与显示器边界完全吻合（±1px）
    const RECT &bounds = info.rcMonitor;
    if (!coordinatesMatch(frame.left, bounds.left)
        || !coordinatesMatch(frame.top, bounds.top)
        || !coordinatesMatch(frame.right, bounds.right)
        || !coordinatesMatch(frame.bottom, bounds.bottom))
        return false;

    if (gamesOnly && !isGameWindow(window, processId))
        return false;

    // 期间前台又变了：这次不算（避免在切换瞬间误判）
    DWORD finalPid = 0;
    return ::GetForegroundWindow() == foreground
           && ::GetWindowThreadProcessId(window, &finalPid) != 0 && finalPid == processId;
}

} // namespace

namespace FullscreenDetector {

bool focusedFullscreenWindowExists(quintptr screenHandle)
{
    Q_UNUSED(screenHandle);
    return matchesFullscreen(::GetForegroundWindow(), false);
}

bool focusedFullscreenGameWindowExists()
{
    return matchesFullscreen(::GetForegroundWindow(), true);
}

} // namespace FullscreenDetector
