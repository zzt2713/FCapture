#include "annotate/shapes/NumberShape.h"

#include <QJsonObject>
#include <QLineF>
#include <QMap>

namespace {

// 各分组的当前计数（下次使用的序号）
QMap<int, int> &counters()
{
    static QMap<int, int> s_counters;
    return s_counters;
}

} // namespace

NumberShape::NumberShape()
{
    m_color = QColor(0xFF, 0x4A, 0x4A);
    m_penWidth = 4;
}

int NumberShape::peekNumber(int groupId)
{
    return counters().value(groupId, 1);
}

int NumberShape::nextNumber(int groupId)
{
    const int n = peekNumber(groupId);
    counters()[groupId] = n + 1;
    return n;
}

void NumberShape::resetNumbering(int groupId)
{
    counters()[groupId] = 1;
}

void NumberShape::resetAllNumbering()
{
    counters().clear();
}

Annotation::Type NumberShape::type() const
{
    return Type::Number;
}

qreal NumberShape::radius() const
{
    // 两位数圆略大，保证数字不挤
    const qreal base = qMax<qreal>(10.0, m_penWidth * 1.5);
    return m_number >= 10 ? base + 2.0 : base;
}

QRectF NumberShape::rawBoundingRect() const
{
    const qreal r = radius();
    return QRectF(m_center.x() - r, m_center.y() - r, r * 2, r * 2);
}

void NumberShape::paintContent(QPainter *p) const
{
    const qreal r = radius();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(m_color);
    p->drawEllipse(m_center, r, r);

    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setBold(true);
    f.setPixelSize(qMax(10, qRound(r * 1.15)));
    p->setFont(f);
    p->setPen(Qt::white);
    p->drawText(QRectF(m_center.x() - r, m_center.y() - r, r * 2, r * 2),
                Qt::AlignCenter, QString::number(m_number));
    p->restore();
}

bool NumberShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    const QPointF d = pt - m_center;
    const qreal rr = radius() + tolerance;
    return QPointF::dotProduct(d, d) <= rr * rr;
}

void NumberShape::translate(const QPointF &delta)
{
    m_center += delta;
}

void NumberShape::scaleAbout(const QPointF &center, qreal factor)
{
    m_center = center + (m_center - center) * factor;
    m_penWidth = qMax(1, qRound(m_penWidth * factor));
}

QVector<QPointF> NumberShape::rawHandles() const
{
    // 单锚点：拖它改半径
    const qreal r = radius();
    return {QPointF(m_center.x() + r, m_center.y() + r)};
}

void NumberShape::moveHandleRaw(int index, const QPointF &pos)
{
    if (index != 0)
        return;
    // 半径 → 笔宽（radius = penWidth * 1.5）
    const qreal r = QLineF(m_center, pos).length();
    m_penWidth = qBound(2, qRound(r / 1.5), 60);
}

QJsonObject NumberShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("number");
    o[QStringLiteral("number")] = m_number;
    o[QStringLiteral("groupId")] = m_groupId;
    o[QStringLiteral("x")] = m_center.x();
    o[QStringLiteral("y")] = m_center.y();
    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("rotation")] = m_rotation;
    return o;
}

std::shared_ptr<Annotation> NumberShape::clone() const
{
    auto copy = std::make_shared<NumberShape>();
    copy->m_number = m_number;
    copy->m_groupId = m_groupId;
    copy->m_center = m_center;
    copy->m_color = m_color;
    copy->m_penWidth = m_penWidth;
    copy->m_rotation = m_rotation;
    return copy;
}
