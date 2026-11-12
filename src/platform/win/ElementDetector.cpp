#include "platform/win/ElementDetector.h"

#include "core/ConfigManager.h"
#include "platform/win/WindowDetector.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <windows.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>

using Microsoft::WRL::ComPtr;

namespace {

qreal dprAtPhysical(const POINT &pt)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect geo = screen->geometry();
        if (geo.contains(pt.x, pt.y))
            return screen->devicePixelRatio();
    }
    return qGuiApp ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
}

QRectF rectToLogical(const RECT &r, qreal dpr)
{
    if (dpr <= 0.0)
        dpr = 1.0;
    return QRectF(r.left / dpr, r.top / dpr,
                  (r.right - r.left) / dpr, (r.bottom - r.top) / dpr);
}

QString controlTypeName(CONTROLTYPEID id)
{
    switch (id) {
    case UIA_ButtonControlTypeId:       return QStringLiteral("按钮");
    case UIA_CalendarControlTypeId:     return QStringLiteral("日历");
    case UIA_CheckBoxControlTypeId:     return QStringLiteral("复选框");
    case UIA_ComboBoxControlTypeId:     return QStringLiteral("下拉框");
    case UIA_EditControlTypeId:         return QStringLiteral("输入框");
    case UIA_HyperlinkControlTypeId:    return QStringLiteral("链接");
    case UIA_ImageControlTypeId:        return QStringLiteral("图像");
    case UIA_ListItemControlTypeId:     return QStringLiteral("列表项");
    case UIA_ListControlTypeId:         return QStringLiteral("列表");
    case UIA_MenuBarControlTypeId:      return QStringLiteral("菜单栏");
    case UIA_MenuItemControlTypeId:     return QStringLiteral("菜单项");
    case UIA_ProgressBarControlTypeId:  return QStringLiteral("进度条");
    case UIA_RadioButtonControlTypeId:  return QStringLiteral("单选框");
    case UIA_ScrollBarControlTypeId:    return QStringLiteral("滚动条");
    case UIA_SliderControlTypeId:       return QStringLiteral("滑块");
    case UIA_SpinnerControlTypeId:      return QStringLiteral("数值框");
    case UIA_StatusBarControlTypeId:    return QStringLiteral("状态栏");
    case UIA_TabControlTypeId:          return QStringLiteral("选项卡");
    case UIA_TabItemControlTypeId:      return QStringLiteral("选项卡项");
    case UIA_TextControlTypeId:         return QStringLiteral("文本");
    case UIA_ToolBarControlTypeId:      return QStringLiteral("工具栏");
    case UIA_ToolTipControlTypeId:      return QStringLiteral("提示");
    case UIA_TreeControlTypeId:         return QStringLiteral("树");
    case UIA_TreeItemControlTypeId:     return QStringLiteral("树节点");
    case UIA_WindowControlTypeId:       return QStringLiteral("窗口");
    case UIA_PaneControlTypeId:         return QStringLiteral("面板");
    case UIA_DocumentControlTypeId:     return QStringLiteral("文档");
    case UIA_GroupControlTypeId:        return QStringLiteral("分组");
    case UIA_TitleBarControlTypeId:     return QStringLiteral("标题栏");
    case UIA_SeparatorControlTypeId:    return QStringLiteral("分隔条");
    case UIA_DataGridControlTypeId:     return QStringLiteral("表格");
    case UIA_DataItemControlTypeId:     return QStringLiteral("数据项");
    case UIA_HeaderControlTypeId:       return QStringLiteral("表头");
    case UIA_HeaderItemControlTypeId:   return QStringLiteral("表头项");
    default:                            return QStringLiteral("元素");
    }
}

QString msaaRoleName(long role)
{
    switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON: return QStringLiteral("按钮");
    case ROLE_SYSTEM_CHECKBUTTON: return QStringLiteral("复选框");
    case ROLE_SYSTEM_RADIOBUTTON: return QStringLiteral("单选框");
    case ROLE_SYSTEM_TEXT:        return QStringLiteral("文本");
    case ROLE_SYSTEM_GRAPHIC:     return QStringLiteral("图像");
    case ROLE_SYSTEM_LINK:        return QStringLiteral("链接");
    case ROLE_SYSTEM_LIST:        return QStringLiteral("列表");
    case ROLE_SYSTEM_LISTITEM:    return QStringLiteral("列表项");
    case ROLE_SYSTEM_MENUITEM:    return QStringLiteral("菜单项");
    case ROLE_SYSTEM_MENUPOPUP:   return QStringLiteral("菜单");
    case ROLE_SYSTEM_CLIENT:      return QStringLiteral("客户区");
    case ROLE_SYSTEM_PANE:        return QStringLiteral("面板");
    case ROLE_SYSTEM_WINDOW:      return QStringLiteral("窗口");
    case ROLE_SYSTEM_COMBOBOX:    return QStringLiteral("下拉框");
    case ROLE_SYSTEM_SLIDER:      return QStringLiteral("滑块");
    case ROLE_SYSTEM_PAGETAB:     return QStringLiteral("选项卡项");
    case ROLE_SYSTEM_TOOLBAR:     return QStringLiteral("工具栏");
    default:                      return QStringLiteral("元素");
    }
}

// MSAA 降级路径：更老的接口。
//
// 对比 AccessibleObjectFromPoint，这里优先用"从目标窗口的 client 对象
// 做 accHitTest"：截图遮罩会干扰 AccessibleObjectFromPoint，而从一个已知
// HWND 出发不会。目标窗口拿不到时才退回全局坐标命中。
QVector<ElementInfo> detectWithMsaa(const POINT &pt, quintptr targetHwnd)
{
    QVector<ElementInfo> out;

    ComPtr<IAccessible> acc;
    VARIANT child;
    ::VariantInit(&child);

    bool got = false;
    if (targetHwnd) {
        ComPtr<IAccessible> windowAcc;
        if (SUCCEEDED(::AccessibleObjectFromWindow(
                reinterpret_cast<HWND>(targetHwnd),
                static_cast<DWORD>(static_cast<LONG>(OBJID_CLIENT)),
                IID_IAccessible,
                reinterpret_cast<void **>(windowAcc.GetAddressOf())))
            && windowAcc) {
            VARIANT hit;
            ::VariantInit(&hit);
            if (SUCCEEDED(windowAcc->accHitTest(static_cast<LONG>(pt.x),
                                                static_cast<LONG>(pt.y), &hit))) {
                if (hit.vt == VT_DISPATCH && hit.pdispVal) {
                    ComPtr<IAccessible> childAcc;
                    if (SUCCEEDED(hit.pdispVal->QueryInterface(
                            IID_IAccessible,
                            reinterpret_cast<void **>(childAcc.GetAddressOf())))
                        && childAcc) {
                        acc = childAcc;
                        ::VariantInit(&child);
                        child.vt = VT_I4;
                        child.lVal = CHILDID_SELF;
                        got = true;
                    }
                } else if (hit.vt == VT_I4) {
                    acc = windowAcc;
                    child = hit;
                    got = true;
                }
            }
            if (!got)
                ::VariantClear(&hit);
        }
    }

    if (!got) {
        ComPtr<IAccessible> globalAcc;
        if (FAILED(::AccessibleObjectFromPoint(pt, globalAcc.GetAddressOf(), &child)))
            return out;
        acc = globalAcc;
    }

    const qreal dpr = dprAtPhysical(pt);

    // 上溯层数从 12 降到 8：每层都要跨进程调 accLocation / get_accName /
    // get_accRole / get_accParent，4 次往返。用户实际只会翻几层，
    // 8 层足够覆盖"控件 → 容器 → 窗口"，还能省掉约 1/3 的往返开销。
    for (int depth = 0; acc && depth < 8; ++depth) {
        ElementInfo info;
        info.depth = depth;

        long x = 0, y = 0, w = 0, h = 0;
        if (SUCCEEDED(acc->accLocation(&x, &y, &w, &h, child))) {
            const RECT r{ x, y, x + w, y + h };
            info.logicalRect = rectToLogical(r, dpr);
        }

        BSTR name = nullptr;
        if (SUCCEEDED(acc->get_accName(child, &name)) && name) {
            info.name = QString::fromWCharArray(name);
            ::SysFreeString(name);
        }

        VARIANT role;
        ::VariantInit(&role);
        if (SUCCEEDED(acc->get_accRole(child, &role)) && role.vt == VT_I4)
            info.controlType = msaaRoleName(role.lVal);
        ::VariantClear(&role);

        if (info.logicalRect.isValid() && !info.logicalRect.isEmpty())
            out.append(info);

        ComPtr<IDispatch> parentDispatch;
        if (FAILED(acc->get_accParent(parentDispatch.GetAddressOf())) || !parentDispatch)
            break;

        ComPtr<IAccessible> parent;
        if (FAILED(parentDispatch.As(&parent)) || !parent)
            break;

        acc = std::move(parent);

        // 父对象的 child 一定是 SELF
        ::VariantInit(&child);
        child.vt = VT_I4;
        child.lVal = CHILDID_SELF;
    }

    ::VariantClear(&child);
    return out;
}

// MSAA 结果是不是只有"窗口壳"级别的粗命中。
//
// Qt / WPF / Electron 的 MSAA 桥常常只暴露顶层窗口或客户区一大块。
// 以前只要 MSAA 非空就直接返回，UIA 兜底永远没机会跑 —— 这就是
// "有的软件识别不到具体控件"的主要原因。粗命中时继续走 UIA 拿更具体的块。
bool isCoarseHit(const QVector<ElementInfo> &hierarchy, quintptr targetHwnd)
{
    if (hierarchy.isEmpty() || !targetHwnd)
        return false;

    RECT wr{};
    if (!::GetWindowRect(reinterpret_cast<HWND>(targetHwnd), &wr)
        || wr.right <= wr.left || wr.bottom <= wr.top)
        return false;

    const POINT center{ (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    const QRectF win = rectToLogical(wr, dprAtPhysical(center));
    const QRectF hit = hierarchy.first().logicalRect;
    if (!hit.isValid() || hit.isEmpty() || win.isEmpty())
        return false;

    // 命中块和窗口一样大（±3px，留 DWM 边框误差）→ 就是窗口壳
    if (qAbs(hit.width() - win.width()) <= 3.0
        && qAbs(hit.height() - win.height()) <= 3.0)
        return true;

    // 层级很浅（只到客户区/面板）且覆盖窗口 85% 以上 → 同样视为粗命中
    return hierarchy.size() <= 2
           && hit.width() * hit.height() >= win.width() * win.height() * 0.85;
}

qreal hitArea(const QVector<ElementInfo> &hierarchy)
{
    if (hierarchy.isEmpty())
        return 0.0;
    const QRectF r = hierarchy.first().logicalRect;
    return r.width() * r.height();
}

} // namespace

// ---------------------------------------------------------------- Worker

// 最新一次请求的 id：UIA 精化任务开始前用它判断自己是否已被新请求取代。
// 移动鼠标时旧精化直接作废，流水线不被 300ms 的 UIA 拖慢（识别框一顿一顿的根因）
std::atomic<quint64> g_latestDetectId{0};

// 快照元素总量上限：大应用（浏览器/IDE）一棵树几万个节点，
// 全抓既慢又没意义（命中测试只需要矩形）。超出就停。
constexpr int kMaxSnapshotElements = 20000;

// ---------------------------------------------------------------- 懒加载树
//
// 对照 snow 的 WindowTree（snow-ui-selector/src/windows/uia/cache.rs）。
//
// 核心思想：每个窗口一棵树，节点按需展开；**展开过的子级永久缓存**，
// 后续查询同一分支零跨进程 UIA 调用。这是 snow 悬停流畅的根本原因
// （它的测试：同位置命中 10 次只产生 2 次 provider 调用）。
//
// 我们之前的做法是每次请求从窗口根重新下钻，等于每次都付全款；
// 全量抓树则相反——一次抓太多，ControlView 又裁掉具体控件。
constexpr int kTreeMaxSteps = 80;      // 单次查询最多下降层数
constexpr int kTreeMaxRects = 100;     // 单次查询最多返回矩形数
constexpr int kTreeLinearLimit = 16;   // 子节点 ≤16 用线性扫，否则建索引

// 子节点命中项：矩形 + 节点下标
struct TreeChild
{
    RECT  bounds{};
    int   node = -1;
};

struct TreeNode
{
    RECT  bounds{};
    int   parent = -1;
    // Unloaded = 还没取过子级；Loaded = 子级已缓存（可为空）；
    // Failed = 该节点展开失败，不再重试
    enum class State { Unloaded, Loaded, Failed } state = State::Unloaded;
    QVector<TreeChild> children;
    // 子节点很多时（弹幕、列表、表格，几百个兄弟）线性扫描会明显拖慢，
    // 这里按 top 坐标做行分桶：命中时只扫目标点所在的那两三个桶。
    // 空桶不占内存；桶大小按整体纵向范围均分。
    QVector<QVector<int>> rowBuckets;   // 每桶存 children 下标
    int bucketTop = 0;                  // 分桶覆盖的纵向起点
    int bucketHeight = 0;               // 每桶高度（像素）
    int bucketCount = 0;                // 桶数量（0 = 未建立，退化线性扫描）
    ComPtr<IUIAutomationElement> element;   // Unloaded 时用于 BuildUpdatedCache
    bool timeoutRetried = false;
};

// 单个窗口的懒加载 UIA 树。只在 UIA 工作线程内使用，不跨线程。
class UiaLazyTree
{
public:
    void setRoot(quintptr hwnd, const RECT &bounds)
    {
        m_hwnd = hwnd;
        m_nodes.clear();
        TreeNode root;
        root.bounds = bounds;
        m_nodes.append(root);
    }

    bool isValid() const { return !m_nodes.isEmpty(); }
    bool hasLoaded() const { return m_loadedAny; }

    // 命中查询：从根逐层下降，返回 元素(深) → … → 窗口(浅) 的矩形链。
    // 只有未展开的节点才会触发一次 BuildUpdatedCache，其余全读缓存。
    QVector<RECT> hitTest(IUIAutomation2 *automation,
                          IUIAutomationCacheRequest *request,
                          const POINT &pt,
                          int budgetMs,
                          const QRect &screenLogical,
                          qreal dpr)
    {
        Q_UNUSED(screenLogical);
        Q_UNUSED(dpr);
        QVector<RECT> path;
        if (m_nodes.isEmpty() || !containsPoint(m_nodes[0].bounds, pt))
            return path;

        QElapsedTimer clock;
        clock.start();
        int current = 0;
        int depth = 0;

        for (; depth < kTreeMaxSteps; ++depth) {
            if (clock.elapsed() >= budgetMs)
                break;
            if (!expand(automation, request, current, budgetMs - int(clock.elapsed())))
                break;

            const int child = hitChild(current, pt);
            if (child < 0)
                break;
            current = child;
        }

        // 从叶子往上收集（跳过与根同尺寸的）
        int node = current;
        const RECT rootBounds = m_nodes[0].bounds;
        while (node > 0 && path.size() < kTreeMaxRects - 1) {
            const RECT &b = m_nodes[node].bounds;
            const bool dup = std::any_of(path.cbegin(), path.cend(),
                                         [&b](const RECT &r) { return sameRect(r, b); });
            if (!sameRect(b, rootBounds) && !dup)
                path.append(b);
            node = m_nodes[node].parent;
        }
        path.append(rootBounds);
        return path;
    }

    // 纯本地命中：只走已展开的节点，**绝不**触发 BuildUpdatedCache。
    // 鼠标连续移动时用它（树已经建立的窗口内零跨进程调用）。
    // 返回 false 表示下钻路径上遇到未展开节点，需要调用方走完整 hitTest。
    bool hitTestCachedOnly(const POINT &pt, QVector<RECT> *out) const
    {
        if (!out || m_nodes.isEmpty() || !containsPoint(m_nodes[0].bounds, pt))
            return false;

        int current = 0;
        for (int depth = 0; depth < kTreeMaxSteps; ++depth) {
            const TreeNode &n = m_nodes.at(current);
            if (n.state == TreeNode::State::Unloaded || n.state == TreeNode::State::Failed)
                return false;   // 路径上还有没展开的，需要完整查询

            const int child = hitChild(current, pt);
            if (child < 0)
                break;
            current = child;
        }

        QVector<RECT> path;
        const RECT rootBounds = m_nodes.at(0).bounds;
        int node = current;
        while (node > 0 && path.size() < kTreeMaxRects - 1) {
            const RECT &b = m_nodes[node].bounds;
            const bool dup = std::any_of(path.cbegin(), path.cend(),
                                         [&b](const RECT &r) {
                                             return r.left == b.left && r.top == b.top
                                                    && r.right == b.right && r.bottom == b.bottom;
                                         });
            if (!(b.left == rootBounds.left && b.top == rootBounds.top
                  && b.right == rootBounds.right && b.bottom == rootBounds.bottom)
                && !dup)
                path.append(b);
            node = m_nodes[node].parent;
        }
        path.append(rootBounds);
        *out = path;
        return true;
    }

    // 清空缓存（窗口结构变化时由上层调用）
    void invalidate() { m_nodes.clear(); }

private:
    static bool containsPoint(const RECT &r, const POINT &p)
    {
        return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
    }

    static bool sameRect(const RECT &a, const RECT &b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }

    // 命中本节点的哪个子级：取"最后一个"包含光标的（后绘制=上层，snow 同款）
    int hitChild(int node, const POINT &pt) const
    {
        const TreeNode &n = m_nodes.at(node);
        const QVector<TreeChild> &kids = n.children;

        // 有分桶索引：只看目标点覆盖到的那一两个桶。
        // 弹幕/列表这类一层几百个兄弟的场景，线性扫描是主要延迟来源。
        if (n.bucketCount > 0 && n.bucketHeight > 0) {
            const int rel = pt.y - n.bucketTop;
            const int b = rel / n.bucketHeight;
            if (b >= 0 && b < n.bucketCount) {
                // 取「最后一个包含该点」的兄弟（后绘制=上层），与线性扫描语义一致。
                // 跨桶的兄弟（高度跨越多桶）会被登记进每个覆盖到的桶。
                int best = -1;
                for (int idx : n.rowBuckets.at(b)) {
                    const TreeChild &c = kids.at(idx);
                    if (containsPoint(c.bounds, pt) && idx > best)
                        best = idx;
                }
                if (best >= 0)
                    return kids.at(best).node;
                // 桶内没命中 → 该点落在桶边界附近，退回线性扫描兜底
            }
        }

        for (int i = kids.size() - 1; i >= 0; --i) {
            if (containsPoint(kids.at(i).bounds, pt))
                return kids.at(i).node;
        }
        return -1;
    }

    // 展开一个节点。只有 State::Unloaded 才跨进程，Loaded 直接返回 true。
    //
    // 注意：**不能持有 `TreeNode &` 引用**。本函数中途会 m_nodes.append
    // 新节点导致 QVector 重分配，旧引用立刻悬空——之前就是这么崩的
    // （0xC0000005）。所有节点访问一律按下标。
    bool expand(IUIAutomation2 *automation, IUIAutomationCacheRequest *request,
                int node, int remainingMs)
    {
        const TreeNode::State state = m_nodes.at(node).state;
        if (state == TreeNode::State::Loaded)
            return true;
        if (state == TreeNode::State::Failed)
            return false;

        // 需要在重分配后仍然可用的数据：先拷出来
        ComPtr<IUIAutomationElement> nodeElement = m_nodes.at(node).element;
        bool timeoutRetried = m_nodes.at(node).timeoutRetried;

        // 动态调整超时：取剩余预算（snow 的 set_timeouts 同款）
        const int ms = qBound(1, remainingMs, 500);
        automation->put_ConnectionTimeout(ms);
        automation->put_TransactionTimeout(ms);

        // 根节点：用 ElementFromHandleBuildCache 直接拿带缓存的窗口根
        ComPtr<IUIAutomationElement> cached;
        HRESULT hr = E_FAIL;
        if (node == 0) {
            hr = automation->ElementFromHandleBuildCache(
                reinterpret_cast<UIA_HWND>(m_hwnd), request, cached.GetAddressOf());
        } else if (nodeElement) {
            hr = nodeElement->BuildUpdatedCache(request, cached.GetAddressOf());
        }
        // 失败且是首次超时：重试一次（snow 的 retry_timeout 同款）
        if (FAILED(hr) && !timeoutRetried) {
            timeoutRetried = true;
            m_nodes[node].timeoutRetried = true;
            if (node == 0) {
                hr = automation->ElementFromHandleBuildCache(
                    reinterpret_cast<UIA_HWND>(m_hwnd), request, cached.GetAddressOf());
            } else if (nodeElement) {
                hr = nodeElement->BuildUpdatedCache(request, cached.GetAddressOf());
            }
        }
        if (FAILED(hr) || !cached) {
            m_nodes[node].state = TreeNode::State::Failed;
            static int __fail = 0;
            if ((__fail++ % 20) == 0)
                qInfo().nospace() << "[expand-fail] node=" << node
                                  << " hr=0x" << QString::number(quint32(hr), 16)
                                  << " hwnd=" << quintptr(m_hwnd);
            return false;
        }

        int count = 0;
        const RECT rootBounds = m_nodes.at(0).bounds;
        QVector<TreeChild> entries;
        ComPtr<IUIAutomationElementArray> kids;
        if (SUCCEEDED(cached->GetCachedChildren(kids.GetAddressOf())) && kids) {
            kids->get_Length(&count);
            entries.reserve(count);
            for (int i = 0; i < count; ++i) {
                ComPtr<IUIAutomationElement> kid;
                if (FAILED(kids->GetElement(i, kid.GetAddressOf())) || !kid)
                    continue;

                BOOL offscreen = FALSE;
                kid->get_CachedIsOffscreen(&offscreen);
                if (offscreen)
                    continue;

                RECT r{};
                if (FAILED(kid->get_CachedBoundingRectangle(&r))
                    || r.right <= r.left || r.bottom <= r.top)
                    continue;
                // 与窗口矩形求交（UIA 偶尔报越界矩形）
                RECT clipped{};
                if (!::IntersectRect(&clipped, &r, &rootBounds)
                    || clipped.right <= clipped.left || clipped.bottom <= clipped.top)
                    continue;

                const int index = m_nodes.size();
                TreeNode child;
                child.bounds = clipped;
                child.parent = node;
                child.element = kid;
                m_nodes.append(child);
                entries.append({ clipped, index });
            }
        }

        // UIA 拿不到子级时退回 HWND 子窗口（snow 的 visible_child_window_rects 同款）。
        //
        // 典型场景：Windows 任务栏的应用图标区（MSTaskSwWClass）是 owner-drawn，
        // UIA 的 ControlView/RawView 都返回空，但它的子窗口 HWND 是存在的
        // （实测 EnumChildWindows 能拿到 17 个，其中 hwnd 66036 正是图标区）。
        // 没有这一步就只能选到"正在运行的应用"整块，选不到具体图标。
        if (entries.isEmpty()) {
            HWND selfHwnd = nullptr;
            if (node == 0) {
                selfHwnd = reinterpret_cast<HWND>(m_hwnd);
            } else if (nodeElement) {
                UIA_HWND nw = nullptr;
                if (SUCCEEDED(nodeElement->get_CurrentNativeWindowHandle(&nw)))
                    selfHwnd = reinterpret_cast<HWND>(nw);
            }
            // 只取**直接子窗口**（GetWindow(GW_CHILD) + GW_HWNDNEXT）。
            // EnumChildWindows 会递归枚举全部后代，矩形互相嵌套，命中测试会乱。
            for (HWND hw = selfHwnd ? ::GetWindow(selfHwnd, GW_CHILD) : nullptr; hw;
                 hw = ::GetWindow(hw, GW_HWNDNEXT)) {
                if (!::IsWindowVisible(hw))
                    continue;
                RECT r{};
                if (!::GetWindowRect(hw, &r) || r.right <= r.left || r.bottom <= r.top)
                    continue;
                RECT clipped{};
                if (!::IntersectRect(&clipped, &r, &rootBounds)
                    || clipped.right - clipped.left <= 1
                    || clipped.bottom - clipped.top <= 1)
                    continue;
                TreeNode child;
                child.bounds = clipped;
                child.parent = node;
                child.state = TreeNode::State::Loaded;   // 叶子，不再展开
                const int index = m_nodes.size();
                m_nodes.append(child);
                entries.append({ clipped, index });
            }
        }

        // 兄弟很多时建立行分桶索引（对齐 snow 的"子节点 > 16 用 R-tree"思路，
        // 这里用更轻的行分桶）。不建索引时 hitChild 要线性扫全部兄弟，
        // 弹幕/长列表一层几百个元素时，鼠标每次移动都会卡在这一步。
        constexpr int kBucketThreshold = 24;
        if (entries.size() > kBucketThreshold) {
            int top = INT_MAX;
            int bottom = INT_MIN;
            for (const TreeChild &c : entries) {
                top = qMin(top, int(c.bounds.top));
                bottom = qMax(bottom, int(c.bounds.bottom));
            }
            const int span = qMax(1, bottom - top);
            // 每桶约 64px，桶数上限 512（避免超大列表建太多空桶）
            const int bucketH = qMax(16, span / 128);
            const int bucketN = qMin(512, qMax(1, (span + bucketH - 1) / bucketH + 1));

            QVector<QVector<int>> buckets(bucketN);
            for (int i = 0; i < entries.size(); ++i) {
                const RECT &r = entries.at(i).bounds;
                int b0 = (int(r.top) - top) / bucketH;
                int b1 = (int(r.bottom) - top) / bucketH;
                b0 = qBound(0, b0, bucketN - 1);
                b1 = qBound(0, b1, bucketN - 1);
                for (int b = b0; b <= b1; ++b)
                    buckets[b].append(i);
            }

            m_nodes[node].rowBuckets = std::move(buckets);
            m_nodes[node].bucketTop = top;
            m_nodes[node].bucketHeight = bucketH;
            m_nodes[node].bucketCount = bucketN;
        }

        m_nodes[node].children = entries;
        m_nodes[node].state = TreeNode::State::Loaded;
        m_nodes[node].element.Reset();   // 子级已展开，父元素引用不再需要
        m_loadedAny = true;

        static int __n = 0;
        if ((__n++ % 20) == 0) {
            qInfo().nospace() << "[expand] node=" << node << " uiaKids=" << count
                              << " entries=" << entries.size() << " hwnd=" << quintptr(m_hwnd)
                              << (entries.isEmpty()
                                      ? QString()
                                      : QStringLiteral(" first=")
                                            + QString::number(entries.first().bounds.right
                                                              - entries.first().bounds.left)
                                            + "x"
                                            + QString::number(entries.first().bounds.bottom
                                                              - entries.first().bounds.top));
        }
        return true;
    }

    quintptr m_hwnd = 0;
    QVector<TreeNode> m_nodes;
    bool m_loadedAny = false;
};

class ElementDetector::Worker : public QObject
{
    Q_OBJECT

public:
    explicit Worker(QObject *parent = nullptr) : QObject(parent) {}
    ~Worker() override = default;

public slots:
    void initialize()
    {
        // 必须用 CUIAutomation8：只有它支持 IUIAutomation2（ConnectionTimeout /
        // TransactionTimeout）。用 CUIAutomation 时 uia.As(&uia2) 会失败，
        // 导致超时设置全部无效、懒加载树分支也进不去（实测 auto2=false）。
        ComPtr<IUIAutomation> uia;
        HRESULT hr = ::CoCreateInstance(CLSID_CUIAutomation8, nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(uia.GetAddressOf()));
        if (FAILED(hr) || !uia) {
            // 系统太老（Win7）时退回 CUIAutomation
            hr = ::CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(uia.GetAddressOf()));
        }
        if (SUCCEEDED(hr) && uia) {
            ComPtr<IUIAutomation2> uia2;
            if (SUCCEEDED(uia.As(&uia2)) && uia2) {
                uia2->put_ConnectionTimeout(80);
                uia2->put_TransactionTimeout(80);
            } else {
                qWarning() << "ElementDetector: IUIAutomation2 unavailable, timeouts not set";
            }
            m_uia = std::move(uia);
        }
    }

    // 清空懒加载树缓存（窗口结构变化 / 新一次截图会话时调用）
    void clearTreeCache() { m_trees.clear(); }

    // 快照：遍历所有可见顶层窗口的 UIA 树，收集全部元素矩形。
    // 只在进入截图时调用一次（Snow Shot 同款思路），之后鼠标移动
    // 全部走本地矩形命中测试，不再跨进程调 UIA。
    void snapshot(quint64 token, QVector<quintptr> exclude)
    {
        QVector<ElementInfo> all;
        if (!m_uia) {
            emit snapshotReady(token, all, true);
            return;
        }

        m_activeToken = token;

        // CacheRequest 批量缓存（snow-ui-selector 的 NativeProvider 同款）：
        // 一次跨进程调用缓存整棵子树的 矩形/离屏/类型/名称，之后零往返读取。
        ComPtr<IUIAutomationCacheRequest> req;
        if (FAILED(m_uia->CreateCacheRequest(req.GetAddressOf())) || !req) {
            emit snapshotReady(token, all, true);
            return;
        }
        req->AddProperty(UIA_BoundingRectanglePropertyId);
        req->AddProperty(UIA_IsOffscreenPropertyId);
        req->AddProperty(UIA_ControlTypePropertyId);
        req->AddProperty(UIA_NamePropertyId);
        req->put_TreeScope(TreeScope_Subtree);
        m_cacheReq = req;

        // 控件视图条件（uia.rs 的 SetTreeFilter(ControlViewCondition) 同款）：
        // 只收控件，滤掉文本碎片/纯布局面板 —— 它们常与按钮同矩形，
        // 混进来会让命中测试"差一点"。
        ComPtr<IUIAutomationCondition> viewCond;
        if (FAILED(m_uia->get_ControlViewCondition(viewCond.GetAddressOf())) || !viewCond) {
            if (FAILED(m_uia->CreateTrueCondition(viewCond.GetAddressOf())) || !viewCond) {
                emit snapshotReady(token, all, true);
                return;
            }
        }
        m_trueCond = viewCond;

        // 抓快照期间放宽 UIA 超时（交互查询仍是 80ms）
        ComPtr<IUIAutomation2> uia2;
        if (SUCCEEDED(m_uia.As(&uia2)) && uia2) {
            uia2->put_ConnectionTimeout(2000);
            uia2->put_TransactionTimeout(2000);
        }

        // ---- 阶段 1：收集候选窗口（廉价，只取 HWND/位置）----
        //
        // 候选窗口统一走 WindowDetector：IsIconic / DWM cloaked / 壳窗口 /
        // 点击穿透 / 配置排除 / 遮罩排除都在里面。以前这里自己写过滤、
        // 只查了 IsWindowVisible——最小化窗口的 UIA 元素以"还原矩形"混进
        // 快照，就是"最小化的窗口也能识别到元素"的根因。
        struct WinEntry { HWND hwnd; qreal dpr; QRectF logicalRect; };
        QVector<WinEntry> wins;

        for (const WindowInfo &w : WindowDetector::enumTopLevelWindows()) {
            if (exclude.contains(w.hwnd))
                continue;
            qreal dpr = 1.0;
            for (QScreen *screen : QGuiApplication::screens()) {
                if (screen->geometry().contains(w.logicalRect.center())) {
                    dpr = screen->devicePixelRatio();
                    break;
                }
            }
            wins.append({ reinterpret_cast<HWND>(w.hwnd), dpr, w.logicalRect });
        }

        // 快速种子先发"光标真命中"的窗口（WindowFromPoint 语义）：
        // 几何包含会把 NVIDIA Overlay 这类全屏悬浮层排到真窗口前面
        const quintptr hitHwnd = WindowDetector::windowFromPoint(QCursor::pos()).hwnd;
        for (int i = 0; i < wins.size(); ++i) {
            if (quintptr(wins.at(i).hwnd) == hitHwnd) {
                const WinEntry w = wins.takeAt(i);
                wins.prepend(w);
                break;
            }
        }
        POINT curPhys{ 0, 0 };
        ::GetCursorPos(&curPhys);

        // ---- 快速种子：先把光标下的命中链发出去 ----
        //
        // snow 同款：光标在哪个窗口就只下钻那棵树的命中路径，
        // 得到"元素 → 父 → 窗"的结构化链。不等整棵扫描。
        if (!wins.isEmpty()) {
            ComPtr<IUIAutomationElement> root;
            if (SUCCEEDED(m_uia->ElementFromHandle(
                    reinterpret_cast<UIA_HWND>(wins.first().hwnd), root.GetAddressOf()))
                && root) {
                QVector<ElementInfo> chain = hitPathAt(root.Get(), curPhys, wins.first().dpr);
                for (ElementInfo &info : chain) {
                    info.nativeHandle = quintptr(wins.first().hwnd);
                    info.logicalRect = info.logicalRect.intersected(wins.first().logicalRect);
                }
                chain.erase(std::remove_if(chain.begin(), chain.end(),
                                           [](const ElementInfo &e) {
                                               return e.logicalRect.isEmpty();
                                           }),
                            chain.end());
                if (!chain.isEmpty()) {
                    m_snapshotBuffer += chain;
                    emit snapshotReady(token, m_snapshotBuffer, false);
                }
            }
        }

        // ---- 阶段 2：逐窗口排队扫描 ----
        //
        // 每个窗口一个独立的排队任务，而不是一个任务从头跑到尾：
        // 之前整棵树扫描独占 worker 几秒，实时查询全被堵在队列里。
        // 拆开后实时请求能插队，鼠标移到未扫完的窗口也有响应。
        for (const WinEntry &w : wins) {
            QMetaObject::invokeMethod(this,
                                      [this, token, w]() {
                                          scanOneWindow(token, w.hwnd, w.dpr, w.logicalRect);
                                      },
                                      Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, token]() { finishSnapshot(token); },
                                  Qt::QueuedConnection);
    }

    // 单窗口扫描：全量抓这棵 UIA 树（FindAllBuildCache 一次往返拿全部属性）。
    //
    // 为什么必须全量：鼠标会在窗口内任意位置移动，快照必须覆盖全部元素，
    // 否则"识别不出来"。层级正确性不靠这里保证——命中时再按真实 UIA 树
    // 逐层下钻（见 hitPathAt / q），矩形只用于快速筛选。
    void scanOneWindow(quint64 token, HWND hwnd, qreal dpr, const QRectF &windowRect)
    {
        if (token != m_activeToken || !m_uia)
            return;   // 已被新快照取代
        if (m_snapshotBuffer.size() >= kMaxSnapshotElements)
            return;

        ComPtr<IUIAutomationElement> root;
        if (FAILED(m_uia->ElementFromHandle(
                reinterpret_cast<UIA_HWND>(hwnd), root.GetAddressOf()))
            || !root) {
            return;
        }

        ComPtr<IUIAutomationElementArray> arr;
        const HRESULT hr = root->FindAllBuildCache(
            TreeScope_Subtree, m_trueCond.Get(), m_cacheReq.Get(), arr.GetAddressOf());
        if (FAILED(hr) || !arr) {
            snapshotWindowTree(root.Get(), dpr, 12, &m_snapshotBuffer,
                               quintptr(hwnd), windowRect);
            emit snapshotReady(token, m_snapshotBuffer, false);
            return;
        }

        int count = 0;
        arr->get_Length(&count);
        for (int i = 0; i < count; ++i) {
            if (m_snapshotBuffer.size() >= kMaxSnapshotElements)
                break;

            ComPtr<IUIAutomationElement> el;
            if (FAILED(arr->GetElement(i, el.GetAddressOf())) || !el)
                continue;

            BOOL offscreen = FALSE;
            el->get_CachedIsOffscreen(&offscreen);
            if (offscreen)
                continue;

            RECT r{};
            if (FAILED(el->get_CachedBoundingRectangle(&r))
                || r.right <= r.left || r.bottom <= r.top)
                continue;

            ElementInfo info;
            info.logicalRect = rectToLogical(r, dpr);
            if (info.logicalRect.isEmpty())
                continue;
            // 只做"完全不越界"的软裁剪：元素中心必须在窗口内。
            // 不能直接 intersected(windowRect)——Chrome 这类把元素矩形
            // 报得比窗口略大（滚动条/装饰），一裁就变空被丢掉，
            // 表现为"只识别到大框，识别不到具体元素"。
            if (!windowRect.contains(info.logicalRect.center()))
                continue;
            info.nativeHandle = quintptr(hwnd);

            BSTR name = nullptr;
            if (SUCCEEDED(el->get_CachedName(&name)) && name) {
                info.name = QString::fromWCharArray(name);
                ::SysFreeString(name);
            }
            CONTROLTYPEID ct = 0;
            if (SUCCEEDED(el->get_CachedControlType(&ct)))
                info.controlType = controlTypeName(ct);

            m_snapshotBuffer.append(info);
        }

        emit snapshotReady(token, m_snapshotBuffer, false);
    }

    // 按需补抓某个窗口的命中链（光标移到新窗口时用）
    void finishSnapshot(quint64 token)
    {
        if (token != m_activeToken)
            return;

        // 恢复交互查询的短超时
        ComPtr<IUIAutomation2> uia2;
        if (SUCCEEDED(m_uia.As(&uia2)) && uia2) {
            uia2->put_ConnectionTimeout(80);
            uia2->put_TransactionTimeout(80);
        }

        qInfo() << "Element snapshot:" << m_snapshotBuffer.size() << "elements";
        emit snapshotReady(token, m_snapshotBuffer, true);
        m_snapshotBuffer.clear();
    }

    // 元素检测主入口：MSAA 优先（毫秒级），粗命中时让 UIA 精化。
    void detect(quint64 requestId, QPoint screenPos, quintptr targetHwnd, bool useMsaa)
    {
        const POINT probe{ screenPos.x(), screenPos.y() };
        const qreal dpr = dprAtPhysical(probe);
        const POINT pt{ qRound(screenPos.x() * dpr), qRound(screenPos.y() * dpr) };

        // UIA 是主路径（snow 同款：它默认后台就是 UIA，MSAA 只作为可选降级）。
        // 以前这里"MSAA 结果不粗就直接返回"，导致 Chrome 这类
        // MSAA 桥只暴露窗口/标签栏的程序**永远进不了 UIA**，
        // 高亮停在大块上、具体控件识别不出来。
        //
        // MSAA 现在只用于：显式配置降级（useMsaa）、或者 UIA 完全不可用。
        if (useMsaa || !m_uia) {
            emit resultReady(requestId, detectWithMsaa(pt, targetHwnd), false);
            return;
        }

        // 懒加载树已经热了（这个窗口展开过）：**跳过 MSAA**。
        //
        // 以前每次请求都同步跑一遍 MSAA（AccessibleObjectFromWindow +
        // accHitTest，跨进程几十毫秒），而它的结果只用来"垫第一帧"。
        // 树热之后纯本地命中是微秒级 —— 为了垫帧先花几十毫秒等 MSAA，
        // 反而成了拖动时的主要延迟（弹幕/长列表场景尤其明显）。
        QVector<ElementInfo> msaaResult;
        const auto warmIt = m_trees.constFind(targetHwnd);
        const bool treeWarm = (warmIt != m_trees.constEnd() && warmIt->hasLoaded());
        if (!treeWarm) {
            msaaResult = detectWithMsaa(pt, targetHwnd);
            if (!msaaResult.isEmpty() && !isCoarseHit(msaaResult, targetHwnd))
                emit resultReady(requestId, msaaResult, true);
        }

        // 必须**同步**调用精化：detect() 本身就运行在 worker 线程里，
        // 再 invokeMethod(QueuedConnection) 只是把任务挪到本线程的事件队列尾部。
        // 而主线程那边 m_elementPending 已经清掉，鼠标一动就会发新请求，
        // 排队中的旧精化执行时 requestId != g_latestDetectId → 直接丢弃。
        // 结果是"鼠标在动就一直不做 UIA 查询"，树永远长不出来（实测日志：
        // [detect] 有输出、[expand] 一次都没有）。
        // 懒加载树本身有缓存，重复命中零跨进程调用，同步执行不会拖慢。
        refineWithUia(requestId, pt, targetHwnd, dpr, msaaResult);
    }

signals:
    void resultReady(quint64 requestId, const QVector<ElementInfo> &hierarchy,
                     bool interim);
    void snapshotReady(quint64 token, const QVector<ElementInfo> &elements,
                       bool final);

private:
    // ---- 快照：遍历一个窗口的 UIA 树，收集所有元素矩形 ----
    // 深度限制避免在大型应用（Office、IDE）上爆炸；
    // 宽度/高度为 0 的容器跳过（对命中测试无意义）。
    void snapshotWindowTree(IUIAutomationElement *root, qreal dpr, int maxDepth,
                            QVector<ElementInfo> *out, quintptr ownerHwnd,
                            const QRectF &windowRect)
    {
        if (!root || !out)
            return;

        struct Item { IUIAutomationElement *el; int depth; };
        QVector<Item> stack;
        stack.append({root, 0});
        root->AddRef();

        while (!stack.isEmpty()) {
            const Item it = stack.takeLast();

            RECT r{};
            if (SUCCEEDED(it.el->get_CurrentBoundingRectangle(&r))
                && r.right > r.left && r.bottom > r.top) {
                ElementInfo info;
                info.logicalRect = QRectF(r.left / dpr, r.top / dpr,
                                          (r.right - r.left) / dpr,
                                          (r.bottom - r.top) / dpr)
                                       .intersected(windowRect);
                if (info.logicalRect.isEmpty()) {
                    it.el->Release();
                    continue;
                }
                info.nativeHandle = ownerHwnd;
                BSTR name = nullptr;
                if (SUCCEEDED(it.el->get_CurrentName(&name)) && name) {
                    info.name = QString::fromWCharArray(name);
                    ::SysFreeString(name);
                }
                CONTROLTYPEID ct = 0;
                if (SUCCEEDED(it.el->get_CurrentControlType(&ct)))
                    info.controlType = QString::number(ct);
                info.depth = it.depth;
                out->append(info);
            }

            if (it.depth < maxDepth) {
                ComPtr<IUIAutomationTreeWalker> walker;
                if (m_uia && SUCCEEDED(m_uia->get_ControlViewWalker(walker.GetAddressOf()))) {
                    ComPtr<IUIAutomationElement> child;
                    if (SUCCEEDED(walker->GetFirstChildElement(it.el, child.GetAddressOf()))
                        && child) {
                        // 必须把兄弟节点全收进来：GetFirstChildElement 只给第一个孩子，
                        // 不沿 GetNextSiblingElement 走一遍就只收集到每层最左边一条链，
                        // 快照里只剩窗口级矩形（按钮/输入框全丢）。
                        // 每层限制 kMaxSiblings 个，防止大列表/表格拖垮快照。
                        constexpr int kMaxSiblings = 300;
                        for (int n = 0; child && n < kMaxSiblings; ++n) {
                            ComPtr<IUIAutomationElement> next;
                            walker->GetNextSiblingElement(child.Get(), next.GetAddressOf());
                            stack.append({child.Detach(), it.depth + 1});
                            child = std::move(next);
                        }
                    }
                }
            }
            it.el->Release();
        }
    }

    // UIA 精化：走懒加载树查询（snow 同款）。
    //
    // 树按窗口缓存并跨请求常驻：第一次进入某窗口会逐层展开命中路径，
    // 之后在同一分支里移动**零跨进程 UIA 调用**，只读本地矩形。
    // 这是"snow 悬停流畅"的核心，我们之前每次请求都从根重建，等于每次付全款。
    void refineWithUia(quint64 requestId, POINT pt, quintptr targetHwnd, qreal dpr,
                       const QVector<ElementInfo> &msaaResult)
    {
        {
            static int __r = 0;
            if ((__r++ % 10) == 0)
                qInfo().nospace() << "[refine] id=" << requestId
                                  << " latest=" << g_latestDetectId.load()
                                  << " hwnd=" << targetHwnd;
        }
        if (requestId != g_latestDetectId.load()) {
            static int __s = 0;
            if ((__s++ % 10) == 0)
                qInfo().nospace() << "[refine-skip] id=" << requestId
                                  << " latest=" << g_latestDetectId.load();
            return;
        }

        QVector<ElementInfo> hierarchy;
        // 本轮懒加载树是否真的可用（决定后面要不要走昂贵的兜底路径）
        bool treeUsable = false;

        if (targetHwnd && m_uia) {
            if (!m_cacheRequest) {
                ComPtr<IUIAutomationCacheRequest> req;
                if (SUCCEEDED(m_uia->CreateCacheRequest(req.GetAddressOf())) && req) {
                    // 只取"自己 + 直接子级"一层（snow 的 TreeScope_Element|Children）
                    req->AddProperty(UIA_BoundingRectanglePropertyId);
                    req->AddProperty(UIA_IsOffscreenPropertyId);
                    req->AddProperty(UIA_ControlTypePropertyId);
                    req->AddProperty(UIA_NamePropertyId);
                    // NativeWindowHandle：UIA 子级为空时要用它回退到 HWND 子窗口
                    //（任务栏应用图标区就是这种情况，必须缓存，否则
                    // get_CurrentNativeWindowHandle 会触发实时跨进程查询）
                    req->AddProperty(UIA_NativeWindowHandlePropertyId);
                    req->put_TreeScope(
                        static_cast<TreeScope>(TreeScope_Element | TreeScope_Children));
                    ComPtr<IUIAutomationCondition> cond;
                    if (SUCCEEDED(m_uia->get_ControlViewCondition(cond.GetAddressOf()))
                        && cond)
                        req->put_TreeFilter(cond.Get());
                    m_cacheRequest = req;
                }
            }

            ComPtr<IUIAutomation2> automation2;
            m_uia.As(&automation2);
            if (m_cacheRequest && automation2) {
                UiaLazyTree &tree = m_trees[targetHwnd];
                if (!tree.isValid()) {
                    RECT wr{};
                    if (::GetWindowRect(reinterpret_cast<HWND>(targetHwnd), &wr))
                        tree.setRoot(targetHwnd, wr);
                }
                if (tree.isValid()) {
                    treeUsable = true;
                    // 先试纯本地命中：树已展开过就零跨进程调用。
                    // 弹幕/长列表这类场景，鼠标连续滑动时每次移动都能即时出框；
                    // 只有下钻路径上有未展开节点时才走完整查询（会触发 UIA）。
                    QVector<RECT> path;
                    if (!tree.hitTestCachedOnly(pt, &path)) {
                        path = tree.hitTest(automation2.Get(), m_cacheRequest.Get(), pt,
                                            168, QRect(), dpr);
                    }
                    for (const RECT &r : path) {
                        ElementInfo info;
                        info.logicalRect = rectToLogical(r, dpr);
                        info.nativeHandle = targetHwnd;
                        hierarchy.append(info);
                    }
                }
            }
        }

        // 兜底只在"懒加载树压根不可用"时跑。
        // 以前只要本次路径为空就再走一整套 ElementFromHandle + 逐层下钻 +
        // 上溯（每步都是跨进程调用），鼠标在空白/纯色区域移动时每次都白跑，
        // 表现为识别明显变慢。树可用但本点没有更深元素，是正常结果，
        // 不该再补一次昂贵查询。
        if (hierarchy.isEmpty() && !treeUsable) {
            ComPtr<IUIAutomationElement> hit;
            if (targetHwnd) {
                ComPtr<IUIAutomationElement> root;
                if (SUCCEEDED(m_uia->ElementFromHandle(
                        reinterpret_cast<UIA_HWND>(targetHwnd), root.GetAddressOf()))
                    && root) {
                    deepestElementAt(root.Get(), pt, hit);
                }
            }
            if (hit && !isOwnProcess(hit.Get()))
                hierarchy = buildHierarchyUp(hit.Get(), dpr);
            else if (!msaaResult.isEmpty())
                hierarchy = msaaResult;
        }

        // 精化期间来了新请求就丢弃本次结果（回了也是旧位置的）
        if (requestId != g_latestDetectId.load())
            return;

        // MSAA 粗结果兜底：UIA 也失败时至少还有窗口级命中；
        // 两边都有结果时保留命中块更小（更具体）的那个。
        if (!msaaResult.isEmpty()
            && (hierarchy.isEmpty() || hitArea(msaaResult) < hitArea(hierarchy)))
            hierarchy = msaaResult;

        emit resultReady(requestId, hierarchy, false);
    }

    static bool isOwnProcess(IUIAutomationElement *element)
    {
        if (!element)
            return false;
        UIA_HWND hwnd = nullptr;
        if (FAILED(element->get_CurrentNativeWindowHandle(&hwnd)) || !hwnd)
            return false;
        DWORD pid = 0;
        ::GetWindowThreadProcessId(reinterpret_cast<HWND>(hwnd), &pid);
        return pid != 0 && pid == ::GetCurrentProcessId();
    }

    // snow 式"边下钻边出链"（uia/cache.rs 的 WindowTree::query + path()）：
    // 从窗口根节点逐层下钻，每层只保留包含光标的那个子节点，把路径上的
    // 矩形按 深→浅 收集成链。
    //
    // 与"全量抓整棵树再按面积猜父子"相比，这是唯一能保证层级正确的方式：
    // 面积相同/矩形交叉的兄弟、列表项、被遮挡元素，靠事后排序永远会猜错。
    QVector<ElementInfo> hitPathAt(IUIAutomationElement *root, const POINT &pt, qreal dpr)
    {
        QVector<ElementInfo> path;
        if (!root || !m_uia)
            return path;

        ComPtr<IUIAutomationTreeWalker> walker;
        if (FAILED(m_uia->get_ControlViewWalker(walker.GetAddressOf())) || !walker)
            return path;

        const QRect screenLogical = logicalVirtualScreen();
        ComPtr<IUIAutomationElement> current(root);

        for (int depth = 0; current && depth < 16; ++depth) {
            // 本级元素：收进链（整屏根节点跳过，高亮它等于整屏不发暗）
            RECT r{};
            if (SUCCEEDED(current->get_CurrentBoundingRectangle(&r))
                && r.right > r.left && r.bottom > r.top) {
                BOOL offscreen = FALSE;
                current->get_CurrentIsOffscreen(&offscreen);
                ElementInfo info;
                info.logicalRect = rectToLogical(r, dpr);
                info.depth = depth;
                const bool fullScreen =
                    info.logicalRect.width() >= screenLogical.width() - 2
                    && info.logicalRect.height() >= screenLogical.height() - 2;
                if (!offscreen && !fullScreen && !info.logicalRect.isEmpty()) {
                    BSTR name = nullptr;
                    if (SUCCEEDED(current->get_CurrentName(&name)) && name) {
                        info.name = QString::fromWCharArray(name);
                        ::SysFreeString(name);
                    }
                    CONTROLTYPEID ct = 0;
                    if (SUCCEEDED(current->get_CurrentControlType(&ct)))
                        info.controlType = controlTypeName(ct);
                    path.append(info);
                }
            }

            // 下一层：取"最后一个"包含光标的子元素（后绘制=在上层，snow 同款）
            ComPtr<IUIAutomationElement> child;
            if (FAILED(walker->GetFirstChildElement(current.Get(), child.GetAddressOf()))
                || !child)
                break;

            ComPtr<IUIAutomationElement> hitChild;
            constexpr int kMaxSiblings = 60;
            int checked = 0;
            for (ComPtr<IUIAutomationElement> it = child; it && checked < kMaxSiblings;
                 ++checked) {
                BOOL offscreen = FALSE;
                it->get_CurrentIsOffscreen(&offscreen);
                RECT cr{};
                if (!offscreen
                    && SUCCEEDED(it->get_CurrentBoundingRectangle(&cr))
                    && cr.right > cr.left && cr.bottom > cr.top
                    && pt.x >= cr.left && pt.x < cr.right
                    && pt.y >= cr.top && pt.y < cr.bottom) {
                    hitChild = it;
                }
                ComPtr<IUIAutomationElement> sibling;
                if (FAILED(walker->GetNextSiblingElement(it.Get(), sibling.GetAddressOf()))
                    || !sibling)
                    break;
                it = sibling;
            }
            if (!hitChild)
                break;
            current = hitChild;
        }
        return path;
    }

    // snow 的核心技巧（uia/cache.rs 的 build_uia_window_cache + query）：
    // 对**本次请求**建一个 CacheRequest，范围 TreeScope_Element|Children，
    // 一次性把整棵子树的矩形/离屏/类型/名称缓存下来。之后逐层下钻时
    // GetCachedChildren() + get_CachedBoundingRectangle() 全是本地读，
    // 零跨进程往返 —— 这才是"按点下钻"不卡的原因。
    //
    // 之前的实现每查一个兄弟就调一次 get_CurrentBoundingRectangle()，
    // 列表控件几百个兄弟就是几百次跨进程调用，慢到只能放弃下钻。
    void deepestElementAt(IUIAutomationElement *root, const POINT &pt,
                          ComPtr<IUIAutomationElement> &out)
    {
        if (!root || !m_uia) {
            out = root;
            return;
        }

        // 建缓存请求：Element | Children，属性一次取全
        ComPtr<IUIAutomationCacheRequest> req;
        if (FAILED(m_uia->CreateCacheRequest(req.GetAddressOf())) || !req) {
            out = root;
            return;
        }
        req->AddProperty(UIA_BoundingRectanglePropertyId);
        req->AddProperty(UIA_IsOffscreenPropertyId);
        req->AddProperty(UIA_ControlTypePropertyId);
        req->AddProperty(UIA_NamePropertyId);
        req->put_TreeScope(static_cast<TreeScope>(TreeScope_Element | TreeScope_Children));
        // 只取控件视图，滤掉纯布局面板（与 snow 的 SetTreeFilter 一致）
        ComPtr<IUIAutomationCondition> viewCond;
        if (SUCCEEDED(m_uia->get_ControlViewCondition(viewCond.GetAddressOf()))
            && viewCond)
            req->put_TreeFilter(viewCond.Get());

        // 用 IUIAutomation::ElementFromHandleBuildCache 拿到"带缓存"的根元素；
        // 之后它派生出的子元素都共享这份缓存。
        HWND rootHwnd = nullptr;
        if (FAILED(root->get_CurrentNativeWindowHandle(
                reinterpret_cast<UIA_HWND *>(&rootHwnd)))
            || !rootHwnd) {
            out = root;
            return;
        }
        ComPtr<IUIAutomationElement> current;
        if (FAILED(m_uia->ElementFromHandleBuildCache(
                reinterpret_cast<UIA_HWND>(rootHwnd), req.Get(),
                current.GetAddressOf()))
            || !current) {
            out = root;
            return;
        }

        // 逐层下钻：每层只取包含光标的**最后一个**子元素（后绘制=更上层，
        // snow 的 hit_before 同款）。所有属性都来自缓存，无跨进程调用。
        for (int depth = 0; depth < 16; ++depth) {
            ComPtr<IUIAutomationElementArray> children;
            if (FAILED(current->GetCachedChildren(children.GetAddressOf())) || !children)
                break;

            int n = 0;
            children->get_Length(&n);
            ComPtr<IUIAutomationElement> hitChild;
            for (int i = 0; i < n; ++i) {
                ComPtr<IUIAutomationElement> child;
                if (FAILED(children->GetElement(i, child.GetAddressOf())) || !child)
                    continue;

                BOOL offscreen = FALSE;
                child->get_CachedIsOffscreen(&offscreen);
                if (offscreen)
                    continue;

                RECT r{};
                if (FAILED(child->get_CachedBoundingRectangle(&r))
                    || r.right <= r.left || r.bottom <= r.top)
                    continue;
                if (pt.x >= r.left && pt.x < r.right
                    && pt.y >= r.top && pt.y < r.bottom) {
                    hitChild = child;   // 继续找后面的兄弟（后面的在上层）
                }
            }
            if (!hitChild)
                break;
            current = hitChild;
        }

        out = current;
    }

    // 从命中元素往上收集到顶层，hierarchy[0] = 最具体的控件。
    // 下钻时已把整棵子树的属性缓存进元素对象，这层的 Current* 调用
    // 对已缓存的元素同样由 UIA 本地解析（不会再跨进程）。
    QVector<ElementInfo> buildHierarchyUp(IUIAutomationElement *hit, qreal dpr)
    {
        QVector<ElementInfo> out;

        ComPtr<IUIAutomationTreeWalker> walker;
        m_uia->get_ControlViewWalker(walker.GetAddressOf());

        ComPtr<IUIAutomationElement> cur(hit);

        const QRect screenLogical = logicalVirtualScreen();
        for (int depth = 0; cur && depth < 16; ++depth) {
            ElementInfo info;
            info.depth = depth;

            RECT r{};
            if (SUCCEEDED(cur->get_CurrentBoundingRectangle(&r))) {
                info.logicalRect = rectToLogical(r, dpr);
                // 整屏元素（桌面根节点）不进列表：高亮它等于整屏不发暗
                if (info.logicalRect.width() >= screenLogical.width() - 2
                    && info.logicalRect.height() >= screenLogical.height() - 2) {
                    info.logicalRect = QRectF();
                }
            }

            BSTR name = nullptr;
            if (SUCCEEDED(cur->get_CurrentName(&name)) && name) {
                info.name = QString::fromWCharArray(name);
                ::SysFreeString(name);
            }

            CONTROLTYPEID typeId = UIA_CustomControlTypeId;
            cur->get_CurrentControlType(&typeId);
            info.controlType = controlTypeName(typeId);

            UIA_HWND native = nullptr;
            if (SUCCEEDED(cur->get_CurrentNativeWindowHandle(&native)))
                info.nativeHandle = quintptr(native);

            if (!info.logicalRect.isEmpty())
                out.append(info);

            if (!walker)
                break;

            ComPtr<IUIAutomationElement> parent;
            if (FAILED(walker->GetParentElement(cur.Get(), parent.GetAddressOf()))
                || !parent) {
                break;
            }
            cur = std::move(parent);
        }
        return out;
    }

    static QRect logicalVirtualScreen()
    {
        QRect r;
        for (QScreen *screen : QGuiApplication::screens())
            r = r.united(screen->geometry());
        return r;
    }

    ComPtr<IUIAutomation> m_uia;
    // 懒加载树缓存：每个窗口一棵，跨请求常驻（snow 同款）。
    // 窗口结构变化/会话结束时由上层清空。
    QHash<quintptr, UiaLazyTree> m_trees;
    ComPtr<IUIAutomationCacheRequest> m_cacheRequest;
    QVector<ElementInfo> m_snapshotBuffer;   // 快照收集缓冲
    // 快照用的批量缓存请求（一次往返取整棵子树的属性）+ 真条件
    ComPtr<IUIAutomationCacheRequest> m_cacheReq;
    ComPtr<IUIAutomationCondition>   m_trueCond;
    quint64 m_activeToken = 0;   // 当前快照 token（旧任务据此作废）
};

// ---------------------------------------------------------------- Thread

class ElementDetector::WorkerThread : public QThread
{
public:
    explicit WorkerThread(QObject *parent = nullptr) : QThread(parent) {}

protected:
    void run() override
    {
        // 必须 MTA：STA 在非 UI 线程里调 UIA 会死锁
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        exec();
        if (SUCCEEDED(hr))
            ::CoUninitialize();
    }
};

// ---------------------------------------------------------------- Facade

ElementDetector *ElementDetector::instance()
{
    static ElementDetector s_instance;
    return &s_instance;
}

ElementDetector::ElementDetector(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<ElementInfo>>("QVector<ElementInfo>");
    // 不在这里建工作线程：SnipOverlay 构造时就会 connect 本单例的信号，
    // 若此时 ensureWorker()，等于每次开始截图都白建一个线程 + COM 公寓。
    // 线程延迟到第一次 request() 时才创建（见 request() 里的 ensureWorker）。
}

ElementDetector::~ElementDetector()
{
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(1500))
            m_thread->terminate();
        m_thread->wait(500);
    }
}

bool ElementDetector::prefersMsaa(const QString &processName)
{
    if (processName.isEmpty())
        return false;

    const QString cfg = ConfigManager::instance()
                            ->value(QStringLiteral("snip/element_msaa_apps"))
                            .toString();
    // 现在默认就 MSAA 优先、UIA 兜底，所以不再需要按进程强制 MSAA。
    // 这里只保留"用户显式配置"的名单：配了就强制走 MSAA（跳过 UIA 兜底），
    // 用于某些 UIA 特别慢、宁可粗一点的程序。
    if (cfg.trimmed().isEmpty())
        return false;
    const QString list = cfg;
    for (const QString &part : list.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (part.trimmed().compare(processName, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void ElementDetector::ensureWorker()
{
    if (m_thread && m_worker)
        return;

    m_thread = new WorkerThread(this);
    m_worker = new Worker();
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Worker::resultReady, this,
            [this](quint64 id, const QVector<ElementInfo> &hierarchy, bool interim) {
                if (id != m_pendingRequestId)
                    return;                 // 已超时或被新请求取代
                // interim（窗口级粗结果）先亮出来但不结单：同一单的 UIA 精化随后就到
                if (!interim)
                    m_pendingRequestId = 0;
                emit detected(id, hierarchy);
            },
            Qt::QueuedConnection);

    connect(m_worker, &Worker::snapshotReady, this,
            [this](quint64 token, const QVector<ElementInfo> &elements, bool final) {
                if (token != m_snapshotToken)
                    return;              // 已被新请求取代
                // 渐进式：每个窗口抓完就发布一次，鼠标下的窗口最先就绪。
                // busy 只在全部完成时解除 —— 中途放行实时请求会和快照
                // 抢 worker，触发 800ms 看门狗把检测永久禁用（踩过）。
                m_snapshotValid = true;
                if (final)
                    m_snapshotBusy = false;
                emit snapshotReady(token, elements);
            },
            Qt::QueuedConnection);

    m_thread->start();

    // 必须排队到工作线程里初始化：run() 里的 CoInitializeEx 要先跑完
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->initialize(); },
                              Qt::QueuedConnection);
}

void ElementDetector::restartWorker()
{
    if (m_restartCount >= kMaxWorkerRestarts) {
        m_disabled = true;
        qWarning() << "ElementDetector: UIA worker hung repeatedly, element detection disabled";
        return;
    }
    ++m_restartCount;

    // 被强杀的线程里可能正跑着快照：状态必须清理，
    // 否则 m_snapshotBusy 永远卡住、request() 永远被拒
    m_snapshotBusy = false;
    m_snapshotValid = false;

    if (m_thread) {
        m_thread->terminate();
        m_thread->wait(500);
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_worker = nullptr;

    ensureWorker();
}

quint64 ElementDetector::requestSnapshot(const QVector<quintptr> &excludeHwnds)
{
    if (m_disabled)
        return 0;

    ensureWorker();

    const quint64 token = ++m_snapshotToken;
    m_snapshotValid = false;
    m_snapshotBusy = true;

    // 新会话：懒加载树缓存必须清空（窗口可能已经变了）
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, [this]() { m_worker->clearTreeCache(); },
                                  Qt::QueuedConnection);

    // 兜底：万一某个 UIA provider 真挂死，30 秒后强制恢复
    //（清 busy 并作废旧快照，让调用方回退到实时检测）
    QTimer::singleShot(30000, this, [this, token]() {
        if (m_snapshotToken == token && m_snapshotBusy) {
            qWarning() << "ElementDetector: snapshot timed out, falling back to live detection";
            m_snapshotBusy = false;
        }
    });

    QMetaObject::invokeMethod(m_worker, [this, token, excludeHwnds]() {
        m_worker->snapshot(token, excludeHwnds);
    }, Qt::QueuedConnection);
    return token;
}

void ElementDetector::clearCache()
{
    if (!m_worker)
        return;
    QMetaObject::invokeMethod(m_worker, [this]() { m_worker->clearTreeCache(); },
                              Qt::QueuedConnection);
}

quint64 ElementDetector::requestRefinement(const QPoint &screenPos, quintptr targetWindow)
{
    if (m_disabled || m_snapshotBusy)
        return 0;
    ensureWorker();

    // 精化沿用普通请求通道：懒加载树已经把命中的分支展开了，
    // 这次查询只会在已缓存节点上多走几层（甚至零跨进程调用），
    // 所以不需要单独的线程/队列。
    return request(screenPos, QString(), targetWindow);
}

quint64 ElementDetector::request(const QPoint &screenPos, const QString &processName,
                                quintptr targetWindow)
{
    if (m_disabled)
        return 0;

    // 快照正在 worker 上跑：排队的实时请求必然超时触发看门狗 →
    // restartWorker ×3 → 检测被永久禁用。直接拒绝，调用方用窗口级高亮兜底。
    if (m_snapshotBusy)
        return 0;

    ensureWorker();

    const quint64 id = m_nextRequestId++;
    m_pendingRequestId = id;
    g_latestDetectId.store(id);   // 工作线程据此作废过期的 UIA 精化

    const bool useMsaa = prefersMsaa(processName);
    QMetaObject::invokeMethod(m_worker, [this, id, screenPos, targetWindow, useMsaa]() {
        m_worker->detect(id, screenPos, targetWindow, useMsaa);
    }, Qt::QueuedConnection);

    // 超时：工作线程大概率卡死在 UIA 调用里，丢弃结果并重建线程
    QTimer::singleShot(kTimeoutMs, this, [this, id]() {
        if (m_pendingRequestId != id)
            return;
        m_pendingRequestId = 0;
        emit timedOut(id);
        restartWorker();
    });

    return id;
}

#include "ElementDetector.moc"
