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
    quintptr nativeHandle = 0;
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
    quint64 request(const QPoint &screenPos, const QString &processName,
                    quintptr targetWindow = 0);

    // 该进程名是否在 MSAA 降级列表里（配置项 snip/element_msaa_apps）
    static bool prefersMsaa(const QString &processName);

    // 连续超时导致永久降级后为 true
    bool isDisabled() const { return m_disabled; }

signals:
    // hierarchy[0] 是光标下最深的元素，越往后越靠父级
    void detected(quint64 requestId, const QVector<ElementInfo> &hierarchy);
    void timedOut(quint64 requestId);

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

    static constexpr int kTimeoutMs = 500;
    static constexpr int kMaxWorkerRestarts = 3;
};
