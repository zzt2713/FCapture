#include "core/Watchdog.h"

#include <QDebug>
#include <QTimer>

Watchdog::Watchdog(QObject *parent)
    : QObject(parent)
{
}

void Watchdog::start(int tickIntervalMs, int stuckThresholdMs)
{
    if (m_timer)
        return;

    m_clock.start();
    m_lastTickMs = 0;

    m_timer = new QTimer(this);
    m_timer->setInterval(tickIntervalMs);

    // 定时器本身就跑在主线程：只有主线程空闲下来才会触发。
    // 若两次触发间隔远超 tick 间隔，就说明中间被长时间占用。
    connect(m_timer, &QTimer::timeout, this, [this, stuckThresholdMs]() {
        const qint64 now = m_clock.elapsed();
        const qint64 gap = now - m_lastTickMs;
        m_lastTickMs = now;

        // 首个 tick 的 gap 就是 tickInterval，天然小于阈值，不会误报
        if (gap > stuckThresholdMs && m_reports < kMaxReports) {
            ++m_reports;
            qWarning() << "Watchdog: main thread was blocked for" << gap << "ms"
                       << "(threshold" << stuckThresholdMs << "ms)";
        }
    });

    m_timer->start();
    qInfo() << "Watchdog: started, tick" << tickIntervalMs << "ms, stuck threshold"
            << stuckThresholdMs << "ms";
}

void Watchdog::stop()
{
    if (!m_timer)
        return;
    m_timer->stop();
    m_timer->deleteLater();
    m_timer = nullptr;
}
