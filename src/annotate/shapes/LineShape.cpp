#include "annotate/shapes/LineShape.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <cmath>

LineShape::LineShape(Kind kind)
    : m_kind(kind)
{
}

Annotation::Type LineShape::type() const
{
    switch (m_kind) {
    case Kind::Line:
    case Kind::Arrow:
        return Type::Line;
    case Kind::Polyline:
        return Type::Polyline;
    }
    return Type::Line;
}

QRectF LineShape::rawBoundingRect() const
{
    if (m_points.isEmpty())
        return {};

    QRectF r(m_points.first(), QSizeF(0, 0));
    for (const QPointF &pt : m_points)
        r = r.united(QRectF(pt, QSizeF(0, 0)));

    const qreal pad = m_penWidth * 2.5 + 4;   // 留出箭头空间
    return r.adjusted(-pad, -pad, pad, pad);
}

void LineShape::setPoints(const QVector<QPointF> &pts)
{
    m_points = pts;
}

void LineShape::appendPoint(const QPointF &pt)
{
    m_points.append(pt);
}

void LineShape::setLastPoint(const QPointF &pt)
{
    if (m_points.isEmpty())
        m_points.append(pt);
    else
        m_points.last() = pt;
}

qreal LineShape::arrowHeadLength(bool thin, bool large) const
{
    if (thin)
        return qBound<qreal>(5.0, m_penWidth * 1.8, 14.0);
    if (large)
        return qBound<qreal>(12.0, m_penWidth * 5.0, 40.0);
    return qBound<qreal>(8.0, m_penWidth * 3.5, 26.0);
}

void LineShape::drawArrowHead(QPainter *p, const QPointF &from, const QPointF &tip,
                              bool thin, bool large) const
{
    const QLineF line(from, tip);
    const qreal len = line.length();
    if (len < 1.0)
        return;

    const qreal headLen = arrowHeadLength(thin, large);
    const qreal headHalfWidth = headLen * (thin ? 0.32 : 0.42);

    const qreal angle = std::atan2(-line.dy(), line.dx());

    const QPointF tipBase(tip.x() - std::cos(angle) * headLen,
                          tip.y() + std::sin(angle) * headLen);
    const QPointF perp(std::sin(angle) * headHalfWidth, std::cos(angle) * headHalfWidth);

    QPolygonF head;
    head << tip
         << tipBase + perp
         << tipBase - perp;

    p->setPen(Qt::NoPen);
    p->setBrush(m_color);
    p->drawPolygon(head);
}


// 锥形笔杆：起点宽 = 极细（约 1px），终点宽 = 笔宽。
// 双向箭头时起点也要粗，所以按"离最近的箭头端越近越粗"来插值。
void LineShape::drawTaperedArrowShaft(QPainter *p, const QVector<QPointF> &pts,
                                      bool thin, bool large) const
{
    if (pts.size() < 2)
        return;

    const bool doubleHeaded = m_arrowStyle == ArrowStyle::Double
                              || m_arrowStyle == ArrowStyle::DoubleThin;
    const qreal headLen = arrowHeadLength(thin, large);

    // 笔杆末端半径必须等于箭头三角根部的半宽，否则两者衔接处会有一道台阶
    // （之前用 penWidth/2，而三角根部是 headLen*0.42，比笔杆宽得多）。
    // 起点仍是极细，视觉上就是一支笔的收锋。
    const qreal headHalfWidth = headLen * (thin ? 0.32 : 0.42);
    const qreal thickR = qMax<qreal>(1.0, headHalfWidth);
    const qreal thinR = 0.6;

    // 按累计弧长算总长，用于插值
    QVector<qreal> accLen(pts.size(), 0.0);
    for (int i = 1; i < pts.size(); ++i)
        accLen[i] = accLen[i - 1] + QLineF(pts.at(i - 1), pts.at(i)).length();
    const qreal total = accLen.last();
    if (total < 0.5)
        return;

    // 逐个线段生成梯形：起点/终点半径按"靠近哪个箭头端"取
    p->save();
    p->setPen(Qt::NoPen);
    p->setBrush(m_color);
    p->setRenderHint(QPainter::Antialiasing, true);

    for (int i = 1; i < pts.size(); ++i) {
        const QPointF a = pts.at(i - 1);
        const QPointF b = pts.at(i);
        const qreal segLen = QLineF(a, b).length();
        if (segLen < 0.01)
            continue;

        // s ∈ [0,1]：沿整条线进度
        const qreal s0 = accLen.at(i - 1) / total;
        const qreal s1 = accLen.at(i) / total;

        const auto radiusAt = [&](qreal s) {
            if (!doubleHeaded)
                return thinR + (thickR - thinR) * s;   // 尾→头 渐粗
            // 双向：两端都粗，中间细
            return thinR + (thickR - thinR) * qAbs(2.0 * s - 1.0);
        };

        const qreal r0 = radiusAt(s0);
        const qreal r1 = radiusAt(s1);

        const QPointF dir = (b - a) / segLen;
        const QPointF n(-dir.y(), dir.x());

        QPolygonF quad;
        quad << a + n * r0 << b + n * r1 << b - n * r1 << a - n * r0;
        p->drawPolygon(quad);
    }

    // 根部和箭头三角之间不要额外画圆去"补缝"：
    // 那个圆半径是 thickR，比三角根部还宽，会在箭头后面鼓出一个包。
    // 三角本身和笔杆在 base 处宽度一致，交叠绘制已经足够闭合。

    p->restore();
}

void LineShape::paintContent(QPainter *p) const
{
    if (m_points.size() < 2)
        return;

    const bool isArrow = (m_kind == Kind::Arrow);
    const bool thin = m_arrowStyle == ArrowStyle::SingleThin
                      || m_arrowStyle == ArrowStyle::DoubleThin;
    const bool large = m_arrowStyle == ArrowStyle::SingleLarge;
    const bool doubleHeaded = m_arrowStyle == ArrowStyle::Double
                              || m_arrowStyle == ArrowStyle::DoubleThin;

    // 需要画箭头的两端：0 = 起点，末尾 = 终点
    QVector<QPointF> pts = m_points;

    // 笔杆回缩到箭头根部：RoundCap 会让线头多伸出 penWidth/2，
    // 不回缩就会从箭头尖端戳出来。回缩量取箭头长度，
    // 线头刚好停在箭头底边，被三角完全盖住。
    if (isArrow && pts.size() >= 2) {
        const qreal headLen = arrowHeadLength(thin, large);

        const QPointF tip = pts.last();
        const QPointF prev = pts.at(pts.size() - 2);
        const qreal segLen = QLineF(prev, tip).length();
        if (segLen > 1.0) {
            const qreal trim = qMin(headLen, segLen * 0.5);
            pts.last() = tip + (prev - tip) * (trim / segLen);
        }

        if (doubleHeaded) {
            const QPointF head = pts.first();
            const QPointF next = pts.at(1);
            const qreal segLen2 = QLineF(head, next).length();
            if (segLen2 > 1.0) {
                const qreal trim = qMin(headLen, segLen2 * 0.5);
                pts.first() = head + (next - head) * (trim / segLen2);
            }
        }
    }

    if (isArrow) {
        // 箭头笔杆画成"从起点极细 → 箭头处最粗"的锥形，
        // 这是 Snipaste 箭头的观感：整支箭头像一支笔的收锋。
        // 实心多边形比可变宽度的 QPen 更干净，也不会出现接缝。
        drawTaperedArrowShaft(p, pts, thin, large);
    } else {
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(m_color, m_penWidth, m_penStyle, Qt::RoundCap, Qt::RoundJoin));

        if (pts.size() == 2) {
            p->drawLine(pts.first(), pts.last());
        } else {
            QPainterPath path(pts.first());
            for (int i = 1; i < pts.size(); ++i)
                path.lineTo(pts.at(i));
            p->drawPath(path);
        }
    }

    if (!isArrow)
        return;

    // 终点箭头
    const QPointF tip = m_points.last();
    QPointF from = m_points.at(m_points.size() - 2);
    if (QLineF(from, tip).length() < 1.0 && m_points.size() >= 3)
        from = m_points.at(m_points.size() - 3);
    drawArrowHead(p, from, tip, thin, large);

    // 起点箭头（双向）
    if (doubleHeaded && m_points.size() >= 2) {
        const QPointF head = m_points.first();
        const QPointF next = (m_points.size() == 2) ? m_points.last() : m_points.at(1);
        drawArrowHead(p, next, head, thin, large);
    }
}

bool LineShape::hitTestContent(const QPointF &pt, qreal tolerance) const
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

        // 点到线段距离
        const QPointF diff = pt - seg.p1();
        qreal t = (diff.x() * d.x() + diff.y() * d.y()) / len2;
        t = qBound<qreal>(0.0, t, 1.0);
        const QPointF closest = seg.p1() + d * t;
        if (QLineF(pt, closest).length() <= tol)
            return true;
    }
    return false;
}

void LineShape::translate(const QPointF &delta)
{
    for (QPointF &pt : m_points)
        pt += delta;
}

void LineShape::scaleAbout(const QPointF &center, qreal factor)
{
    for (QPointF &pt : m_points)
        pt = center + (pt - center) * factor;
}

QVector<QPointF> LineShape::rawHandles() const
{
    if (m_kind == Kind::Polyline)
        return m_points;

    if (m_points.size() < 2)
        return {};
    return {m_points.first(), m_points.last()};
}

void LineShape::moveHandleRaw(int index, const QPointF &pos)
{
    if (index < 0 || index >= m_points.size())
        return;

    if (m_kind == Kind::Polyline) {
        m_points[index] = pos;
        return;
    }

    // 直线/箭头只有两个端点
    if (index == 0)
        m_points.first() = pos;
    else if (index == 1)
        m_points.last() = pos;
}

QJsonObject LineShape::toJson() const
{
    QJsonObject o;
    const QString kindText = m_kind == Kind::Arrow ? QStringLiteral("arrow")
                                                   : (m_kind == Kind::Polyline
                                                          ? QStringLiteral("polyline")
                                                          : QStringLiteral("line"));
    o[QStringLiteral("type")] = kindText;

    QJsonArray pts;
    for (const QPointF &pt : m_points)
        pts.append(QJsonArray{pt.x(), pt.y()});
    o[QStringLiteral("points")] = pts;

    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("penStyle")] = int(m_penStyle);
    o[QStringLiteral("arrowStyle")] = int(m_arrowStyle);
    return o;
}

std::shared_ptr<Annotation> LineShape::clone() const
{
    auto copy = std::make_shared<LineShape>(m_kind);
    copy->m_points = m_points;
    copy->m_color = m_color;
    copy->m_penWidth = m_penWidth;
    copy->m_penStyle = m_penStyle;
    copy->m_arrowStyle = m_arrowStyle;
    return copy;
}
