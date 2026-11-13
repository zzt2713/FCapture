#pragma once

#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QVector>

struct ElementInfo
{
    QRectF  logicalRect;      // 逻辑像素，屏幕坐标系
    QString name;
    QString controlType;
    int     depth = 0;        // 0 = 光标下最深的元素
    // 实时检测 = 元素自身的原生窗口句柄；快照模式 = 所属顶层窗口 HWND
    //（hitTestSnapshot 按它做归属过滤，被遮挡窗口的元素不参与命中）
    quintptr nativeHandle = 0;
    // 快照模式：父元素在本窗口元素数组里的下标（-1 = 根）。
    // 命中时沿真实 UIA 父子关系上溯组链，不再靠矩形包含"猜"层级。
    int parentIndex = -1;
};

// 界面元素检测（UI Automation）
//
// 线程模型：
//   - 所有 UIA 调用都在一个专用工作线程里执行，主线程永不直接调 UIA
//   - 工作线程用 MTA 初始化 COM（STA 在非 UI 线程会死锁）
//   - 每次请求带 requestId，主线程 500ms 超时后丢弃结果
//   - 超时说明该线程卡在 UIA 调用里，直接 terminate 并重建一个，
//     最多重建 kMaxWorkerRestarts 次，之后永久降级为只检测窗口
//
// 单例，主线程使用。
class ElementDetector : public QObject
{
    Q_OBJECT

public:
    static ElementDetector *instance();

    // 发起一次异步检测。processName 命中 MSAA 降级列表时只走 MSAA。
    // targetWindow：截图遮罩下面那个真实窗口的 HWND。必须传，否则
    // ElementFromPoint 只会命中我们自己的全屏遮罩（等于只能识别整屏）。
    // 返回本次请求的 id；结果通过 detected / timedOut 信号带回。
    // budgetMs：树查询时间预算。默认 168ms（普通悬停）；
    // 精化传 1500ms，让 Edge/Chromium 这类深树能展开到具体控件。
    quint64 request(const QPoint &screenPos, const QString &processName,
                    quintptr targetWindow = 0, int budgetMs = 168);

    // 该进程名是否在 MSAA 降级列表里（配置项 snip/element_msaa_apps）
    static bool prefersMsaa(const QString &processName);

    // ---- 快照模式（Snow Shot 同款）----
    //
    // 进入截图时抓一次所有可见窗口的元素树，鼠标移动时纯本地命中测试。
    // 这样鼠标移动不再触发跨进程 UIA 调用，延迟从 100~300ms 降到微秒级。
    //
    // 返回快照 token；结果通过 snapshotReady 信号带回。
    quint64 requestSnapshot(const QVector<quintptr> &excludeHwnds = {});

    // 清空懒加载树缓存。进入新一次截图会话时调用（snow 的 refresh 同款）：
    // 窗口结构可能已经变了，旧矩形不能继续用。
    void clearCache();

    // 精化请求（snow 的 refinement 同款）：指针停稳后以更大预算再查一次，
    // 让懒加载树把命中路径展开得更深。结果仍通过 detected 信号带回。
    quint64 requestRefinement(const QPoint &screenPos, quintptr targetWindow);
    // 让下一次 requestSnapshot 重建（窗口结构变化时调用）
    void invalidateSnapshot() { m_snapshotValid = false; }
    bool isSnapshotValid() const { return m_snapshotValid; }
    // 快照正在抓取中（可能要几秒）。此时 request() 会拒绝新请求：
    // 快照占用 worker 线程，实时请求必然超过 800ms 看门狗，
    // 会被误判为"worker 挂死"并把检测永久禁用（实测踩过）。
    bool isSnapshotBusy() const { return m_snapshotBusy; }

    // 连续超时导致永久降级后为 true
    bool isDisabled() const { return m_disabled; }

signals:
    // hierarchy[0] 是光标下最深的元素，越往后越靠父级
    void detected(quint64 requestId, const QVector<ElementInfo> &hierarchy);
    void timedOut(quint64 requestId);
    // 快照就绪：elements 按 Z 序排列（越靠后越靠前），每项带完整矩形
    void snapshotReady(quint64 token, const QVector<ElementInfo> &elements);

private:
    explicit ElementDetector(QObject *parent = nullptr);
    ~ElementDetector() override;

    void ensureWorker();
    void restartWorker();

    class Worker;
    class WorkerThread;

    WorkerThread *m_thread = nullptr;
    Worker      *m_worker = nullptr;

    quint64 m_nextRequestId = 1;
    quint64 m_pendingRequestId = 0;
    int     m_restartCount = 0;
    bool    m_disabled = false;

    // 快照状态
    bool    m_snapshotValid = false;
    bool    m_snapshotBusy = false;
    quint64 m_snapshotToken = 0;

    static constexpr int kTimeoutMs = 800;
    static constexpr int kMaxWorkerRestarts = 3;
};
