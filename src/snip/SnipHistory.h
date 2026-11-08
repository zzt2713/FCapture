#pragma once

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>

struct SnipRecord
{
    QImage    image;
    QRect     logicalRect;
    QDateTime timestamp;
};

// 内存中的截图历史（不落盘）
class SnipHistory : public QObject
{
    Q_OBJECT

public:
    static SnipHistory *instance();

    void add(const QImage &image, const QRect &logicalRect);
    bool isEmpty() const { return m_records.isEmpty(); }
    int  count() const { return m_records.size(); }
    const QVector<SnipRecord> &records() const { return m_records; }

    // 从最新往回遍历：offset=0 是最近一条
    const SnipRecord *record(int offset) const;

    QRect lastRect() const;

    void clear();
    void removeAt(int index);
    void setMaxCount(int n);
    int maxCount() const { return m_maxCount; }

signals:
    void changed();

private:
    explicit SnipHistory(QObject *parent = nullptr);

    QVector<SnipRecord> m_records;
    int m_maxCount = 20;
};
