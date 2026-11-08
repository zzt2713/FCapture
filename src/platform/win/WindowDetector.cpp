#include "platform/win/WindowDetector.h"

#include "core/ConfigManager.h"
#include "platform/win/WinUtil.h"

#include <QGuiApplication>
#include <QScreen>
#include <QElapsedTimer>
#include <QStringList>

#include <windows.h>
#include <dwmapi.h>

namespace {

qreal dprAt(const POINT &pt)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect geo = screen->geometry();
        if (geo.contains(pt.x, pt.y))
            return screen->devicePixelRatio();
    }
    return qGuiApp ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
}

QRect toLogical(const RECT &r, qreal dpr)
{
    if (dpr <= 0.0)
        dpr = 1.0;
    return QRect(qRound(r.left / dpr), qRound(r.top / dpr),
                 qRound((r.right - r.left) / dpr), qRound((r.bottom - r.top) / dpr));
}

// 逻辑坐标 → 物理像素
POINT toPhysical(const QPoint &pt)
{
    const POINT probe{ pt.x(), pt.y() };
    const qreal dpr = dprAt(probe);
    return POINT{ qRound(pt.x() * dpr), qRound(pt.y() * dpr) };
}

// 截图遮罩自己的 HWND。必须排除它，否则它永远是最顶层的全屏窗口。
quintptr g_overlayHwnd = 0;

bool isIgnoredWindow(HWND hwnd)
{
    return g_overlayHwnd != 0 && reinterpret_cast<quintptr>(hwnd) == g_overlayHwnd;
}

// 窗口的"可见边框"。
//
// GetWindowRect 返回的是**含不可见缩放边框**的矩形：Win10/11 上四周各多出
// 约 7~8px（Shadow/Border），直接用它会在选区四周留一圈空隙，
// 看起来永远"差一点严丝合缝"。
// DWMWA_EXTENDED_FRAME_BOUNDS 才是用户眼睛看到的那一圈。
bool windowFrameRect(HWND hwnd, RECT *out)
{
    if (!out)
        return false;

    constexpr DWORD kExtendedFrameBounds = 9;   // DWMWA_EXTENDED_FRAME_BOUNDS
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, kExtendedFrameBounds, out,
                                          sizeof(RECT)))) {
        // DWM 对最小化/无边框窗口可能返回空矩形，退回 GetWindowRect
        if (out->right > out->left && out->bottom > out->top)
            return true;
    }
    return ::GetWindowRect(hwnd, out) != FALSE;
}

QString classNameOf(HWND hwnd)
{
    wchar_t cls[256] = {};
    ::GetClassNameW(hwnd, cls, 255);
    return QString::fromWCharArray(cls);
}

QString titleOf(HWND hwnd)
{
    wchar_t title[512] = {};
    ::GetWindowTextW(hwnd, title, 511);
    return QString::fromWCharArray(title);
}

// 系统壳窗口：桌面、壁纸、任务栏内部容器等
bool isShellWindow(const QString &className)
{
    static const QStringList kShellClasses = {
        QStringLiteral("Progman"),
        QStringLiteral("WorkerW"),
        QStringLiteral("Shell_SecondaryTrayWnd"),
        QStringLiteral("Windows.UI.Core.CoreWindow"),
        QStringLiteral("Windows.Internal.Shell.TabProxyWindow"),
        QStringLiteral("XamlExplorerHostIslandWindow"),
    };
    return kShellClasses.contains(className);
}

// 过滤规则（对齐 Snipaste）：
//   1 可见 / 2 未最小化 / 3 不排除 WS_EX_TOOLWINDOW
//   4 排除 WS_EX_LAYERED 但允许点击穿透之外的层（输入法候选框等）
//   5 面积 >= 20×20 / 6 有标题或非系统壳类名
//   7 排除本进程 / 8 排除桌面壁纸 / 9 排除配置里的程序
bool acceptWindow(HWND hwnd, const QString &className)
{
    if (!::IsWindowVisible(hwnd) || ::IsIconic(hwnd))
        return false;
    // 只排除截图遮罩自己。以前排除"整个本进程"，导致首选项/分组管理/
    // 白板这些自家窗口完全无法被识别。
    if (isIgnoredWindow(hwnd))
        return false;

    // 排除配置里指定的程序
    {
        static QStringList excluded;
        static bool loaded = false;
        if (!loaded) {
            const QString cfg = ConfigManager::instance()
                                    ->value(QStringLiteral("snip/exclude_from_detection")).toString();
            for (const QString &part : cfg.split(QLatin1Char(','), Qt::SkipEmptyParts))
                excluded.append(part.trimmed().toLower());
            loaded = true;
        }
        if (!excluded.isEmpty()) {
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid) {
                HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (proc) {
                    wchar_t path[MAX_PATH] = {};
                    DWORD size = MAX_PATH;
                    if (::QueryFullProcessImageNameW(proc, 0, path, &size)) {
                        QString fullPath = QString::fromWCharArray(path);
                        const int slash = fullPath.lastIndexOf(QLatin1Char(92)); // backslash
                        const QString name = (slash >= 0 ? fullPath.mid(slash + 1) : fullPath).toLower();
                        if (excluded.contains(name)) {
                            ::CloseHandle(proc);
                            return false;
                        }
                    }
                    ::CloseHandle(proc);
                }
            }
        }
    }

    const LONG_PTR ex = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // 点击穿透窗口（如桌面小组件）不参与检测
    if (ex & WS_EX_TRANSPARENT)
        return false;

    RECT r{};
    if (!windowFrameRect(hwnd, &r))
        return false;
    if ((r.right - r.left) < 20 || (r.bottom - r.top) < 20)
        return false;

    if (isShellWindow(className))
        return false;

    // 不再要求窗口必须有标题。
    //
    // 以前这里"无标题直接排除"，导致大量真实窗口被丢弃：
    //   - Chromium / Electron 的渲染窗口、UWP 的 CoreWindow 标题常为空
    //   - 弹出面板、浮动工具栏、覆盖层窗口普遍没有标题
    // 结果就是"只能识别当前活动的那个软件"。
    static const QStringList kDecorationClasses = {
        QStringLiteral("tooltips_class32"),
        QStringLiteral("MSCTFIME UI"),
        QStringLiteral("IME"),
        QStringLiteral("ForegroundStaging"),
    };
    if (kDecorationClasses.contains(className))
        return false;

    return true;
}

struct EnumCtx
{
    QVector<WindowInfo> out;
    int z = 0;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<EnumCtx *>(lParam);
    const QString cls = classNameOf(hwnd);

    if (!acceptWindow(hwnd, cls))
        return TRUE;

    RECT r{};
    if (!windowFrameRect(hwnd, &r))
        return TRUE;

    const POINT center{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    const qreal dpr = dprAt(center);

    WindowInfo info;
    info.hwnd = reinterpret_cast<quintptr>(hwnd);
    info.title = titleOf(hwnd);
    info.className = cls;
    info.logicalRect = toLogical(r, dpr);
    info.zOrder = ctx->z++;
    ctx->out.append(info);
    return TRUE;
}

} // namespace

namespace WindowDetector {

void setOverlayWindow(quintptr hwnd)
{
    g_overlayHwnd = hwnd;
}

QVector<WindowInfo> enumTopLevelWindows()
{
    EnumCtx ctx;
    ::EnumWindows(&enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.out;
}

} // namespace WindowDetector

namespace {

// 顶层窗口列表只在"窗口集合可能变化"时重建。
// 鼠标每移动一帧都 EnumWindows + 每个窗口 OpenProcess 查进程名，
// 是截图卡顿的主要来源之一（一帧几十次系统调用）。
// 这里缓存 200ms，同一次鼠标移动里的多次查询直接复用。
QVector<WindowInfo> cachedTopLevelWindows()
{
    static QVector<WindowInfo> cache;
    static QElapsedTimer timer;
    static bool valid = false;

    constexpr qint64 kCacheMs = 200;
    if (valid && timer.isValid() && timer.elapsed() < kCacheMs)
        return cache;

    cache = WindowDetector::enumTopLevelWindows();
    timer.restart();
    valid = true;
    return cache;
}

// 按 Z 序自上而下取光标下的窗口。
//
// 不能用 WindowFromPoint：截图遮罩层本身是全屏置顶窗口，那一步命中的
// 永远是遮罩层自己，再按"排除本进程"过滤后就是空——表现为窗口检测完全失灵。
// 只能走 EnumWindows（Z 序由上到下），逐层跳过本进程窗口，取第一个包含光标的。
QVector<WindowInfo> stackedWindowsAt(const QPoint &screenPos)
{
    QVector<WindowInfo> stack;
    for (const WindowInfo &w : cachedTopLevelWindows()) {
        if (w.logicalRect.contains(screenPos))
            stack.append(w);
    }
    return stack;
}

} // namespace

namespace WindowDetector {

WindowInfo windowFromPoint(const QPoint &screenPos)
{
    const QVector<WindowInfo> stack = stackedWindowsAt(screenPos);
    return stack.isEmpty() ? WindowInfo() : stack.first();
}

QVector<WindowInfo> windowsUnderPoint(const QPoint &screenPos)
{
    return stackedWindowsAt(screenPos);
}

QRect bestWindowRectAt(const QPoint &screenPos)
{
    // 栈顶即最上层可见窗口，正是用户在遮罩下"指向"的那个
    const QVector<WindowInfo> stack = stackedWindowsAt(screenPos);
    return stack.isEmpty() ? QRect() : stack.first().logicalRect;
}

QString processNameAt(const QPoint &screenPos)
{
    const QVector<WindowInfo> stack = stackedWindowsAt(screenPos);
    if (stack.isEmpty())
        return {};

    HWND hwnd = reinterpret_cast<HWND>(stack.first().hwnd);
    DWORD winPid = 0;
    ::GetWindowThreadProcessId(hwnd, &winPid);
    if (winPid == 0)
        return {};

    HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, winPid);
    if (!proc)
        return {};

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString name;
    if (::QueryFullProcessImageNameW(proc, 0, path, &size))
        name = QString::fromWCharArray(path);
    ::CloseHandle(proc);

    const int slash = name.lastIndexOf(QLatin1Char('\\'));
    if (slash >= 0)
        name = name.mid(slash + 1);
    return name.toLower();
}

} // namespace WindowDetector
