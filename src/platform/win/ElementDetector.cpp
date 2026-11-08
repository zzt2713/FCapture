#include "platform/win/ElementDetector.h"

#include "core/ConfigManager.h"

#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <windows.h>
#include <oleacc.h>
#include <UIAutomation.h>
#include <wrl/client.h>

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

} // namespace

// ---------------------------------------------------------------- Worker

class ElementDetector::Worker : public QObject
{
    Q_OBJECT

public:
    explicit Worker(QObject *parent = nullptr) : QObject(parent) {}
    ~Worker() override = default;

public slots:
    void initialize()
    {
        ComPtr<IUIAutomation> uia;
        if (SUCCEEDED(::CoCreateInstance(__uuidof(CUIAutomation), nullptr,
                                         CLSCTX_INPROC_SERVER,
                                         IID_PPV_ARGS(uia.GetAddressOf())))) {
            // 缩短 UIA 自身的跨进程等待（IUIAutomation2 才有这两个属性），
            // 外层还有 500ms 看门狗兜底
            ComPtr<IUIAutomation2> uia2;
            if (SUCCEEDED(uia.As(&uia2)) && uia2) {
                uia2->put_ConnectionTimeout(80);
                uia2->put_TransactionTimeout(80);
            }
            m_uia = std::move(uia);
        }
    }

    void detect(quint64 requestId, QPoint screenPos, quintptr targetHwnd, bool useMsaa)
    {
        QVector<ElementInfo> hierarchy;

        const POINT probe{ screenPos.x(), screenPos.y() };
        const qreal dpr = dprAtPhysical(probe);
        const POINT pt{ qRound(screenPos.x() * dpr), qRound(screenPos.y() * dpr) };

        // 顺序很重要：**MSAA 优先，UIA 兜底**。
        //
        // MSAA 的 AccessibleObjectFromPoint / accHitTest 是"点查询"，
        // 一次调用几毫秒；UIA 要遍历控件树、跨进程取每个节点的
        // BoundingRectangle，动辄几十到几百毫秒 —— 这正是之前卡顿的原因。
        // Snipaste 之所以跟手，就是主用 MSAA。
        //
        // 只有 MSAA 拿不到有用结果（纯 UWP / 部分 WPF）时才走 UIA。
        if (!useMsaa) {
            hierarchy = detectWithMsaa(pt, targetHwnd);
        }

        if (!hierarchy.isEmpty() && !useMsaa) {
            emit resultReady(requestId, hierarchy);
            return;
        }

        if (useMsaa || !m_uia) {
            if (hierarchy.isEmpty())
                hierarchy = detectWithMsaa(pt, targetHwnd);
        } else {
            // 截图时我们自己的全屏遮罩盖在所有窗口之上。ElementFromPoint
            // 会原样返回这个遮罩（整屏），所以它永远只能拿到"整屏"。
            //
            // 正确做法：主线程按 Z 序算出遮罩下面那个真实窗口，用
            // ElementFromHandle 取它的 UIA 根，再从根一路向下找光标命中的
            // 最具体控件。这样既绕开遮罩，又能拿到按钮/输入框/列表项。
            ComPtr<IUIAutomationElement> hit;
            if (targetHwnd) {
                ComPtr<IUIAutomationElement> root;
                if (SUCCEEDED(m_uia->ElementFromHandle(
                        reinterpret_cast<UIA_HWND>(targetHwnd), root.GetAddressOf()))
                    && root) {
                    deepestElementAt(root.Get(), pt, hit);
                }
            }

            // 没有目标窗口句柄（或该窗口不提供 UIA 树）时的兜底：
            // 直接用 ElementFromPoint，命中本进程元素就丢掉，退回 MSAA。
            if (!hit) {
                ComPtr<IUIAutomationElement> fromPoint;
                if (SUCCEEDED(m_uia->ElementFromPoint(pt, fromPoint.GetAddressOf()))
                    && fromPoint && !isOwnProcess(fromPoint.Get())) {
                    hit = fromPoint;
                }
            }

            if (hit) {
                hierarchy = buildHierarchyUp(hit.Get(), dpr);
            }

            // 三层回退：目标窗口 UIA → 全局 ElementFromPoint → MSAA。
            // 很多自绘程序（WPF 老版本、Qt 程序、部分 Electron）单靠某一条
            // 路径都拿不到控件，必须三条都试一遍，这才是 Snipaste 能覆盖
            // 大部分软件的原因。
            if (hierarchy.isEmpty()) {
                ComPtr<IUIAutomationElement> fromPoint;
                if (SUCCEEDED(m_uia->ElementFromPoint(pt, fromPoint.GetAddressOf()))
                    && fromPoint && !isOwnProcess(fromPoint.Get())) {
                    hierarchy = buildHierarchyUp(fromPoint.Get(), dpr);
                }
            }
            if (hierarchy.isEmpty())
                hierarchy = detectWithMsaa(pt, targetHwnd);
        }

        emit resultReady(requestId, hierarchy);
    }

signals:
    void resultReady(quint64 requestId, const QVector<ElementInfo> &hierarchy);

private:
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

    // 从 root 向下，取包含 pt 的最深子元素，写入 out。
    // 只走 ControlView：RawView 会带出大量不可见装饰节点，反而拖慢。
    void deepestElementAt(IUIAutomationElement *root, const POINT &pt,
                          ComPtr<IUIAutomationElement> &out)
    {
        ComPtr<IUIAutomationTreeWalker> walker;
        m_uia->get_ControlViewWalker(walker.GetAddressOf());
        if (!walker) {
            out = root;
            return;
        }

        ComPtr<IUIAutomationElement> current(root);

        // 限制层数：控件树可能非常深，超过 24 层基本是自绘容器，没有意义
        for (int depth = 0; depth < 12; ++depth) {
            ComPtr<IUIAutomationElement> child;
            if (FAILED(walker->GetFirstChildElement(current.Get(), child.GetAddressOf()))
                || !child) {
                break;
            }

            // 遍历兄弟节点找命中的那个。这里是最容易变慢的地方：
            // 列表/表格类控件的兄弟可能有几百上千个，每个都要跨进程取
            // BoundingRectangle。限制最多检查 kMaxSiblings 个，
            // 超出就放弃继续下钻（停在当前层，已经是一个可用的块）。
            constexpr int kMaxSiblings = 40;
            ComPtr<IUIAutomationElement> next;
            int checked = 0;
            for (ComPtr<IUIAutomationElement> it = child; it && checked < kMaxSiblings;
                 ++checked) {
                RECT r{};
                if (SUCCEEDED(it->get_CurrentBoundingRectangle(&r))
                    && r.right > r.left && r.bottom > r.top
                    && pt.x >= r.left && pt.x < r.right
                    && pt.y >= r.top && pt.y < r.bottom) {
                    next = it;
                    break;
                }
                ComPtr<IUIAutomationElement> sibling;
                if (FAILED(walker->GetNextSiblingElement(it.Get(), sibling.GetAddressOf()))
                    || !sibling) {
                    break;
                }
                it = sibling;
            }
            if (!next)
                break;
            current = next;
        }

        out = current;
    }

    // 从命中元素往上收集到顶层，hierarchy[0] = 最具体的控件
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
    ensureWorker();
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
            [this](quint64 id, const QVector<ElementInfo> &hierarchy) {
                if (id != m_pendingRequestId)
                    return;                 // 已超时或被新请求取代
                m_pendingRequestId = 0;
                emit detected(id, hierarchy);
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

    if (m_thread) {
        m_thread->terminate();
        m_thread->wait(500);
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    m_worker = nullptr;

    ensureWorker();
}

quint64 ElementDetector::request(const QPoint &screenPos, const QString &processName,
                                quintptr targetWindow)
{
    if (m_disabled)
        return 0;

    ensureWorker();

    const quint64 id = m_nextRequestId++;
    m_pendingRequestId = id;

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
