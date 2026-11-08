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
    rec.image = image;
    rec.logicalRect = logicalRect;
    rec.timestamp = QDateTime::currentDateTime();

    m_records.prepend(rec);
    while (m_records.size() > m_maxCount)
        m_records.removeLast();
    emit changed();
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
