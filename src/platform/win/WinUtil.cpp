#include "platform/win/WinUtil.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>

#include <windows.h>

#include <string>

namespace WinUtil {

void setProcessDpiAware()
{
    // Per-Monitor V2；失败则退到 Per-Monitor，再退到系统 DPI 感知
    using SetCtxFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = ::GetModuleHandleW(L"user32.dll")) {
        auto setCtx = reinterpret_cast<SetCtxFn>(
            ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setCtx && setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }
    ::SetProcessDPIAware();
}

namespace {

qreal dprForRect(const RECT &r)
{
    // 取窗口中心点所在屏幕的 dpr
    const POINT center{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect geo = screen->geometry();
        if (geo.contains(center.x, center.y))
            return screen->devicePixelRatio();
    }
    return qGuiApp ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
}

QRect toLogical(const RECT &r)
{
    const qreal dpr = dprForRect(r);
    if (dpr <= 0.0)
        return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
    return QRect(qRound(r.left / dpr), qRound(r.top / dpr),
                 qRound((r.right - r.left) / dpr), qRound((r.bottom - r.top) / dpr));
}

// 逻辑坐标 → 物理像素（与 windowRectAt 相同的换算约定）
POINT toPhysicalPoint(const QPoint &logical)
{
    qreal dpr = 1.0;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(logical)) {
            dpr = screen->devicePixelRatio();
            break;
        }
    }
    return POINT{ qRound(logical.x() * dpr), qRound(logical.y() * dpr) };
}

} // namespace

QRect activeWindowRect()
{
    const HWND hwnd = ::GetForegroundWindow();
    if (!hwnd)
        return {};
    RECT r{};
    if (!::GetWindowRect(hwnd, &r))
        return {};
    return toLogical(r);
}

QRect windowRectAt(const QPoint &screenPos)
{
    POINT pt{ screenPos.x(), screenPos.y() };
    // 传入的是逻辑坐标，需要先换算成物理像素
    qreal dpr = 1.0;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(screenPos)) {
            dpr = screen->devicePixelRatio();
            break;
        }
    }
    pt.x = qRound(pt.x * dpr);
    pt.y = qRound(pt.y * dpr);

    HWND hwnd = ::WindowFromPoint(pt);
    if (!hwnd)
        return {};
    hwnd = ::GetAncestor(hwnd, GA_ROOT);
    if (!hwnd)
        return {};

    RECT r{};
    if (!::GetWindowRect(hwnd, &r))
        return {};
    return toLogical(r);
}

void applyToolWindowStyle(quintptr wid)
{
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd)
        return;
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_TOOLWINDOW;
    ex &= ~WS_EX_APPWINDOW;
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

void applyNoActivateStyle(quintptr wid)
{
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd)
        return;
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex |= WS_EX_NOACTIVATE;
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

void forceTopmost(quintptr wid)
{
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd)
        return;
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool isForeignWindowAbove(quintptr wid)
{
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd)
        return false;
    const DWORD selfPid = ::GetCurrentProcessId();
    for (HWND above = ::GetWindow(hwnd, GW_HWNDPREV); above;
         above = ::GetWindow(above, GW_HWNDPREV)) {
        DWORD pid = 0;
        ::GetWindowThreadProcessId(above, &pid);
        if (pid != selfPid)
            return true;
    }
    return false;
}

void setClickThrough(quintptr wid, bool on)
{
    HWND hwnd = reinterpret_cast<HWND>(wid);
    if (!hwnd)
        return;
    LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (on)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
}

void activateWindowAt(const QPoint &logicalPos)
{
    const POINT pt = toPhysicalPoint(logicalPos);
    HWND target = ::WindowFromPoint(pt);
    if (!target)
        return;
    target = ::GetAncestor(target, GA_ROOT);
    if (!target || target == ::GetForegroundWindow())
        return;
    ::SetForegroundWindow(target);
}

void forwardWheel(quintptr selfWid, int delta, const QPoint &logicalPos)
{
    const POINT pt = toPhysicalPoint(logicalPos);
    HWND target = ::WindowFromPoint(pt);
    if (!target)
        return;
    // 自己（含子窗口）不再收，防止转发给自己造成死循环
    const HWND self = reinterpret_cast<HWND>(selfWid);
    if (self && (target == self || ::GetAncestor(target, GA_ROOT) == self))
        return;
    ::PostMessageW(target, WM_MOUSEWHEEL,
                   MAKEWPARAM(0, static_cast<SHORT>(delta)),
                   MAKELPARAM(pt.x, pt.y));
}

void beep()
{
    ::MessageBeep(MB_OK);
}

void trimWorkingSet()
{
    // 释放的工作集不会自动还给 OS，任务管理器的"内存"会一直停在峰值。
    // 两个 (SIZE_T)-1 是文档化的"立即裁剪工作集"用法（kernel32，无额外依赖）。
    ::SetProcessWorkingSetSize(::GetCurrentProcess(), static_cast<SIZE_T>(-1),
                              static_cast<SIZE_T>(-1));
}

bool isFunctionKeyHeld(const QString &keyName)
{
    const QString upper = keyName.trimmed().toUpper();
    if (!upper.startsWith(QLatin1Char('F')))
        return false;

    bool ok = false;
    const int n = upper.mid(1).toInt(&ok);
    if (!ok || n < 1 || n > 24)
        return false;

    return (::GetAsyncKeyState(VK_F1 + n - 1) & 0x8000) != 0;
}

// ---------------------------------------------------------------- 开机自启

namespace {

QString g_lastError;

// 注册表值名直接用可执行文件名，卸载/排查时一眼能认出来
const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t *kRunValue = L"FCapture";

// 自启命令行：带引号包住路径，含空格的安装目录才不会断
QString autoStartCommand()
{
    const QString exe = QCoreApplication::applicationFilePath();
    return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(exe));
}

} // namespace

bool setAutoStart(bool enabled)
{
    HKEY key = nullptr;
    const LONG open = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (open != ERROR_SUCCESS) {
        g_lastError = QStringLiteral("无法打开注册表自启项（错误码 %1）").arg(open);
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enabled) {
        // 注册表统一 UTF-16：中文用户名路径必须走 W 版 API
        const std::wstring cmd = autoStartCommand().toStdWString();
        result = ::RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                  reinterpret_cast<const BYTE *>(cmd.c_str()),
                                  DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        result = ::RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND)
            result = ERROR_SUCCESS;      // 本来就没有，不算失败
    }
    ::RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        g_lastError = QStringLiteral("写入注册表失败（错误码 %1）").arg(result);
        return false;
    }
    g_lastError.clear();
    return true;
}

bool isAutoStartEnabled()
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LONG status = ::RegQueryValueExW(key, kRunValue, nullptr, &type,
                                           reinterpret_cast<BYTE *>(buf), &size);
    ::RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_SZ)
        return false;

    // 只有指向当前 exe 才算启用：换过安装位置后旧路径不该误判为已启用
    const QString stored = QString::fromWCharArray(buf).trimmed();
    return stored.compare(autoStartCommand(), Qt::CaseInsensitive) == 0;
}

QString lastError()
{
    return g_lastError;
}

} // namespace WinUtil
