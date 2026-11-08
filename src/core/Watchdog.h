#pragma once

#include <QElapsedTimer>
#include <QObject>

class QTimer;

// 主线程卡死看门狗。
// 主线程每 tickInterval 打一次点；若某次检查发现距上次打点超过 stuckThreshold，
// 说明主线程被阻塞，立即写一条 WARN 日志（含阻塞时长）。
// 只记录不重启：自动重启会掩盖真正的死循环问题。
class Watchdog : public QObject
{
    Q_OBJECT

public:
    explicit Watchdog(QObject *parent = nullptr);

    void start(int tickIntervalMs = 2000, int stuckThresholdMs = 5000);
    void stop();

    // 主线程持续阻塞时，日志里最多打印这么多条，避免刷爆
    static constexpr int kMaxReports = 5;

private:
    void tick();

    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastTickMs = 0;
    int m_reports = 0;
};
