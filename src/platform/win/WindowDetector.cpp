#include "platform/win/WindowDetector.h"

#include "core/ConfigManager.h"
#include "platform/win/WinUtil.h"

#include <QGuiApplication>
#include <QScreen>
#include <QElapsedTimer>
#include <QHash>
#include <QDebug>
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
//
// DWMWA_EXTENDED_FRAME_BOUNDS 是 DWM 实际合成到屏幕上的窗口外沿，
// 也就是用户眼睛看到的那一圈 —— 不再额外收边，否则会切掉内容。
bool windowFrameRect(HWND hwnd, RECT *out)
{
    if (!out)
        return false;

    constexpr DWORD kExtendedFrameBounds = 9;   // DWMWA_EXTENDED_FRAME_BOUNDS
    if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, kExtendedFrameBounds, out,
                                          sizeof(RECT)))) {
        // DWM 对最小化/无边框窗口可能返回空矩形，退回 GetWindowRect
        if (out->right > out->left && out->bottom > out->top) {
            if (Q_UNLIKELY(qEnvironmentVariableIsSet("FCAPTURE_DEBUG_FRAME"))) {
                RECT wr{};
                ::GetWindowRect(hwnd, &wr);
                qDebug() << "frame" << QString::number(quintptr(hwnd), 16)
                         << "DWM" << out->left << out->top
                         << (out->right - out->left) << (out->bottom - out->top)
                         << "WinRect" << wr.left << wr.top
                         << (wr.right - wr.left) << (wr.bottom - wr.top)
                         << "delta" << (out->left - wr.left) << (out->top - wr.top)
                         << (out->right - wr.right) << (out->bottom - wr.bottom);
            }
            return true;
        }
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

// 全屏游戏/录制叠加层：NVIDIA GeForce Overlay、Discord/Steam/Medal 等。
// 它们全屏覆盖桌面、没有 WS_EX_TRANSPARENT（靠逐像素 alpha 穿透），
// 能通过 acceptWindow 的全部检查，于是永远霸占光标命中——
// 表现为"有窗口打开时任务栏/任何东西都识别不了"（实测 NVIDIA Overlay
// 的 CEF-OSC-WIDGET 排在 Shell_TrayWnd 前面）。
bool isScreenOverlay(const QString &className, const QString &title)
{
    static const QStringList kOverlayClasses = {
        QStringLiteral("CEF-OSC-WIDGET"),          // NVIDIA GeForce Overlay
        QStringLiteral("DiscordOverlay"),
        QStringLiteral("GameOverlayUI"),
        QStringLiteral("MedalOverlay"),
    };
    if (kOverlayClasses.contains(className))
        return true;
    return title.contains(QStringLiteral("Overlay"), Qt::CaseInsensitive)
           || title.contains(QStringLiteral("GeForce Overlay"), Qt::CaseInsensitive);
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
    // UWP 隐藏窗 / 虚拟桌面切换留下的幽灵窗口：DWM 标记 cloaked，
    // 窗口"可见"但屏幕上没有内容（snow 的 is_window_cloaked 同款过滤）
    {
        constexpr DWORD kCloaked = 15;   // DWMWA_CLOAKED
        DWORD cloaked = 0;
        if (SUCCEEDED(::DwmGetWindowAttribute(hwnd, kCloaked, &cloaked,
                                              sizeof(cloaked)))
            && cloaked != 0)
            return false;
    }
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
    if (isScreenOverlay(className, titleOf(hwnd)))
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

// 光标下"真·能点到"的顶层窗口：交给系统判定（WindowFromPoint 的窗口区域 /
// 逐像素透明语义和真实鼠标一致——NVIDIA Overlay 这类 alpha 悬浮窗靠透明像素
// 穿透，任何几何/样式模拟都会把它错当成实心全屏窗，抢走任务栏和底下窗口的命中）。
// 遮罩自己是全屏置顶窗，查询期间临时加 WS_EX_TRANSPARENT 让系统跳过它。
quintptr realRootWindowAt(const QPoint &screenPos)
{
    const POINT pt = toPhysical(screenPos);
    HWND overlay = reinterpret_cast<HWND>(g_overlayHwnd);
    LONG_PTR ex = 0;
    if (overlay) {
        ex = ::GetWindowLongPtrW(overlay, GWL_EXSTYLE);
        ::SetWindowLongPtrW(overlay, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);
    }
    const HWND hit = ::WindowFromPoint(pt);
    if (overlay)
        ::SetWindowLongPtrW(overlay, GWL_EXSTYLE, ex);
    return hit ? quintptr(::GetAncestor(hit, GA_ROOT)) : 0;
}

// 光标下的窗口栈。
//
// 排序规则（实测校准，日志数据见下）：
//   1. 以 WindowFromPoint 的真命中为准 —— 它尊重窗口区域与**逐像素 alpha**，
//      系统认为"光标此刻实际点到的是谁"，就是它。
//   2. 真命中窗口若在可选列表里 → 排栈首，其余按 z 序补在后面（滚轮换层用）。
//   3. 真命中窗口**不在可选列表里**（例如 NVIDIA Overlay 的 CEF 悬浮子窗、
//      被 acceptWindow 过滤掉的窗口）→ 不能简单忽略：这恰恰说明该位置被
//      一个透明浮层覆盖。此时把它上面的候选全部丢掉，从它之后的 z 序继续，
//      否则会把浮层本身当成合法窗口（实测就是这样：光标在任务栏上时
//      stack[0] 是 CEF-OSC-WIDGET，任务栏被顶到第二位）。
//
// 实测日志（修复前）：
//   [stack] pos=(431,1054) all=11 stack=2 "[0]CEF-OSC-WIDGET [1]Shell_TrayWnd"
//   hit=657208   ← hit 是 NVIDIA Overlay 的子窗口，不在缓存里
QVector<WindowInfo> stackedWindowsAt(const QPoint &screenPos)
{
    const quintptr hitRoot = realRootWindowAt(screenPos);
    const QVector<WindowInfo> all = cachedTopLevelWindows();

    // 真命中是桌面/壁纸：没有合法检测目标（不让全屏壳窗口顶上来）
    if (hitRoot != 0 && isShellWindow(classNameOf(reinterpret_cast<HWND>(hitRoot)))) {
        bool selectable = false;
        for (const WindowInfo &w : all) {
            if (w.hwnd == hitRoot) {
                selectable = true;
                break;
            }
        }
        if (!selectable)
            return {};
    }

    // 按 z 序收集所有覆盖该点的窗口（all 本身即 EnumWindows 顺序）
    QVector<WindowInfo> stack;
    for (const WindowInfo &w : all) {
        if (w.logicalRect.contains(screenPos))
            stack.append(w);
    }

    // 真命中窗口在可选列表里：提到栈首
    if (hitRoot != 0) {
        for (int i = 0; i < stack.size(); ++i) {
            if (stack.at(i).hwnd == hitRoot) {
                if (i > 0)
                    stack.move(i, 0);
                return stack;
            }
        }
    }

    // 真命中不在可选列表里（透明浮层 / 被过滤窗口）：
    // 找光标点下"系统真正穿透到的那个窗口"——用最上层**可选**窗口兜底。
    // 但不能停在浮层本身：先确认该位置最上层的合法窗口是谁。
    // 这里的 stack 已经按 z 序，且浮层通常不在 all 里，所以 stack[0]
    // 就是穿透后的第一个真实窗口。直接返回即可。
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

    // 按 HWND 缓存：同一个窗口的进程名不会变，
    // 而 OpenProcess + QueryFullProcessImageName 每次都是几百微秒级开销，
    // 鼠标在同一个窗口上移动时没必要重复问。
    static QHash<quintptr, QString> cache;
    const quintptr key = quintptr(hwnd);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

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
    name = name.toLower();

    // 缓存上限：窗口句柄数量有限，超过 256 就整体清空（简单且足够）
    if (cache.size() > 256)
        cache.clear();
    cache.insert(key, name);
    return name;
}

} // namespace WindowDetector
