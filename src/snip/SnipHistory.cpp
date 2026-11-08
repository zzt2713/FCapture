#include "snip/SnipHistory.h"
#include "core/ConfigManager.h"

SnipHistory::SnipHistory(QObject *parent)
    : QObject(parent)
    , m_maxCount(qMax(1, ConfigManager::instance()
                             ->value(QStringLiteral("general/snip_history_count"))
                             .toInt()))
{
}

SnipHistory *SnipHistory::instance()
{
    static SnipHistory s_instance;
    return &s_instance;
}

void SnipHistory::add(const QImage &image, const QRect &logicalRect)
{
    if (image.isNull() || logicalRect.isEmpty())
        return;

    SnipRecord rec;
    rec.logicalRect = logicalRect;
    rec.timestamp = QDateTime::currentDateTime();

    // 缩略图：内存占用与截图分辨率解耦
    const int longest = qMax(image.width(), image.height());
    if (longest > kThumbMaxSide) {
        rec.image = image.scaled(kThumbMaxSide, kThumbMaxSide, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
    } else {
        rec.image = image;
    }

    // 原图只给最新一条留着（贴图/保存要用），更早的直接丢掉。
    // 这样历史 20 条的内存从"20 × 全图"降到"1 × 全图 + 20 × 缩略图"。
    m_records.prepend(rec);
    m_records.first().fullImage = image;
    if (m_records.size() > 1)
        m_records[1].fullImage = QImage();

    while (m_records.size() > m_maxCount)
        m_records.removeLast();
    emit changed();
}

QImage SnipHistory::imageFor(int index) const
{
    if (index < 0 || index >= m_records.size())
        return {};
    const SnipRecord &rec = m_records.at(index);
    return rec.fullImage.isNull() ? rec.image : rec.fullImage;
}

const SnipRecord *SnipHistory::record(int offset) const
{
    if (offset < 0 || offset >= m_records.size())
        return nullptr;
    return &m_records.at(offset);
}

QRect SnipHistory::lastRect() const
{
    if (m_records.isEmpty())
        return {};
    return m_records.first().logicalRect;
}

void SnipHistory::clear()
{
    if (m_records.isEmpty())
        return;
    m_records.clear();
    emit changed();
}

void SnipHistory::removeAt(int index)
{
    if (index < 0 || index >= m_records.size())
        return;
    m_records.removeAt(index);
    emit changed();
}

void SnipHistory::setMaxCount(int n)
{
    m_maxCount = qMax(1, n);
    const int before = m_records.size();
    while (m_records.size() > m_maxCount)
        m_records.removeLast();
    if (m_records.size() != before)
        emit changed();
}
