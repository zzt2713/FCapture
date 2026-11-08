#include "annotate/shapes/FreehandShape.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPainterPathStroker>

FreehandShape::FreehandShape(Kind kind)
    : m_kind(kind)
{
    if (kind == Kind::Marker) {
        m_color = QColor(0xFF, 0xD6, 0x0A, 100);
        m_penWidth = 12;
    } else {
        m_penWidth = 3;
    }
}

Annotation::Type FreehandShape::type() const
{
    return m_kind == Kind::Pencil ? Type::Pencil : Type::Marker;
}

QRectF FreehandShape::rawBoundingRect() const
{
    if (m_points.isEmpty())
        return {};

    QRectF r(m_points.first(), QSizeF(0, 0));
    for (const QPointF &pt : m_points)
        r = r.united(QRectF(pt, QSizeF(0, 0)));

    const qreal pad = m_penWidth * (m_kind == Kind::Marker ? 2.0 : 1.0) + 2;
    return r.adjusted(-pad, -pad, pad, pad);
}

void FreehandShape::appendPoint(const QPointF &pt)
{
    if (!m_points.isEmpty()) {
        // 距离过近的点丢弃，减少数据量
        // 0.6px 阈值：再大就会丢采样点，快速画线时曲线会变折线（锯齿感来源之一）
        if (QLineF(m_points.last(), pt).length() < 0.6)
            return;
    }
    m_points.append(pt);
}

QPainterPath FreehandShape::buildPath() const
{
    QPainterPath path;
    if (m_points.isEmpty())
        return path;

    path.moveTo(m_points.first());
    if (m_points.size() < 3) {
        path.lineTo(m_points.last());
        return path;
    }

    // 三次贝塞尔（Catmull-Rom 转 Bezier）：
    // 相邻采样点之间用平滑切线连接，曲线在整条笔迹上连续，
    // 不会像"原始点当二次控制点"那样在拐弯处出现尖角和锯齿。
    //
    // 之前的问题：二次贝塞尔只经过中点，控制点就是原始采样点，
    // 采样间距稍大（快速画线、低采样率）时曲线会在控制点附近抖一下，
    // 看起来就是锯齿/折线。
    const auto point = [this](int i) {
        return m_points.at(qBound(0, i, m_points.size() - 1));
    };

    for (int i = 0; i + 1 < m_points.size(); ++i) {
        const QPointF p0 = point(i - 1);
        const QPointF p1 = point(i);
        const QPointF p2 = point(i + 1);
        const QPointF p3 = point(i + 2);

        // Catmull-Rom → 三次贝塞尔控制点（张力 1/6）
        const QPointF c1 = p1 + (p2 - p0) / 6.0;
        const QPointF c2 = p2 - (p3 - p1) / 6.0;
        path.cubicTo(c1, c2, p2);
    }
    return path;
}

void FreehandShape::paintContent(QPainter *p) const
{
    if (m_points.isEmpty())
        return;

    p->setRenderHint(QPainter::Antialiasing);
    p->setBrush(Qt::NoBrush);

    if (!m_ellipseBrush) {
        p->setPen(QPen(m_color, m_penWidth, m_penStyle, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(buildPath());
        return;
    }

    // 椭圆笔头：沿路径每 penWidth/4 盖一个固定朝向的椭圆，合并成一条连续椭圆条纹。
    // alpha 只在合并后的整条路径上填充一次，马克笔不会因叠加变深。
    QPainterPath stamps;
    // 重叠椭圆必须用 WindingFill 合并，OddEvenFill 会让重叠区互相挖空
    stamps.setFillRule(Qt::WindingFill);
    const qreal rx = m_penWidth * 0.95;
    const qreal ry = m_penWidth * 0.5;
    const qreal step = qMax<qreal>(1.0, m_penWidth / 4.0);

    for (int i = 1; i < m_points.size(); ++i) {
        const QLineF seg(m_points.at(i - 1), m_points.at(i));
        const int n = qMax(1, int(seg.length() / step));
        for (int k = 0; k <= n; ++k) {
            const QPointF pt = seg.pointAt(qreal(k) / n);
            stamps.addEllipse(QRectF(pt.x() - rx, pt.y() - ry, rx * 2, ry * 2));
        }
    }
    if (m_points.size() == 1) {
        stamps.addEllipse(QRectF(m_points.first().x() - rx, m_points.first().y() - ry,
                                 rx * 2, ry * 2));
    }

    p->setPen(Qt::NoPen);
    if (m_penStyle == Qt::SolidLine) {
        p->setBrush(m_color);
        p->drawPath(stamps);
    } else {
        // 虚线椭圆笔头：不填充，只画虚线轮廓
        p->setPen(QPen(m_color, 1, m_penStyle));
        p->setBrush(Qt::NoBrush);
        p->drawPath(stamps);
    }
}

bool FreehandShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    if (m_points.size() < 2)
        return false;

    const qreal tol = tolerance + m_penWidth / 2.0;
    for (int i = 1; i < m_points.size(); ++i) {
        const QLineF seg(m_points.at(i - 1), m_points.at(i));
        const QPointF d = seg.p2() - seg.p1();
        const qreal len2 = d.x() * d.x() + d.y() * d.y();
        if (len2 < 0.0001)
            continue;
        const QPointF diff = pt - seg.p1();
        qreal t = (diff.x() * d.x() + diff.y() * d.y()) / len2;
        t = qBound<qreal>(0.0, t, 1.0);
        if (QLineF(pt, seg.p1() + d * t).length() <= tol)
            return true;
    }
    return false;
}

void FreehandShape::translate(const QPointF &delta)
{
    for (QPointF &pt : m_points)
        pt += delta;
}

void FreehandShape::scaleAbout(const QPointF &center, qreal factor)
{
    for (QPointF &pt : m_points)
        pt = center + (pt - center) * factor;
    m_penWidth = qMax(1, qRound(m_penWidth * factor));
}

QVector<QPointF> FreehandShape::rawHandles() const
{
    // 自由曲线只有首尾两个控制点
    if (m_points.size() < 2)
        return {};
    return {m_points.first(), m_points.last()};
}

void FreehandShape::moveHandleRaw(int index, const QPointF &pos)
{
    if (index == 0 && !m_points.isEmpty())
        m_points.first() = pos;
    else if (index == 1 && m_points.size() >= 2)
        m_points.last() = pos;
}

QJsonObject FreehandShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = m_kind == Kind::Pencil ? QStringLiteral("pencil")
                                                       : QStringLiteral("marker");
    QJsonArray pts;
    for (const QPointF &pt : m_points)
        pts.append(QJsonArray{pt.x(), pt.y()});
    o[QStringLiteral("points")] = pts;
    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("penStyle")] = int(m_penStyle);
    o[QStringLiteral("ellipseBrush")] = m_ellipseBrush;
    return o;
}

std::shared_ptr<Annotation> FreehandShape::clone() const
{
    auto copy = std::make_shared<FreehandShape>(m_kind);
    copy->m_points = m_points;
    copy->m_color = m_color;
    copy->m_penWidth = m_penWidth;
    copy->m_penStyle = m_penStyle;
    copy->m_ellipseBrush = m_ellipseBrush;
    return copy;
}
