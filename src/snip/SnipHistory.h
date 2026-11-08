#pragma once

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QVector>

struct SnipRecord
{
    // 只存缩略图，不存原图。
    //
    // 历史目前只有两处用途：
    //   1. `,` `.` 回放 —— 只用 logicalRect，完全不看图
    //   2. 首选项「历史记录」页 —— 只显示 104px 缩略图
    // 存原图时 20 条就是几十上百 MB（4K 截图单张就 30MB+）。
    QImage    image;        // 缩略图（最长边 <= kThumbMaxSide）
    QImage    fullImage;    // 仅"最近一条"保留原图，供贴图/保存用；更早的为 null
    QRect     logicalRect;
    QDateTime timestamp;
};

// 内存中的截图历史（不落盘）
class SnipHistory : public QObject
{
    Q_OBJECT

public:
    static SnipHistory *instance();

    // 最长边超过该值的记录会被缩成缩略图
    static constexpr int kThumbMaxSide = 480;

    void add(const QImage &image, const QRect &logicalRect);
    // 取某条记录的可用原图（没有原图时返回缩略图）
    QImage imageFor(int index) const;
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
