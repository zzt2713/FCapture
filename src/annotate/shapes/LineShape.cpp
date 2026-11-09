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

// 箭头各部分一律从笔宽等比缩放。以前这里用固定上下限（8~26px），
// 笔宽超过 8 以后箭头就再也不变了，表现成"选择大小没有用"。
qreal LineShape::arrowHeadLength(bool thin, bool large) const
{
    const qreal factor = large ? 4.2 : (thin ? 2.4 : 3.2);
    return qMax<qreal>(4.0, m_penWidth * factor);
}

// 箭头三角的半宽：细长实心三角（长:宽 ≈ 2:1），直接接在笔杆上，
// 不加粗不鼓包；细头更收、大头稍放
qreal LineShape::arrowHeadHalfWidth(qreal headLen, bool thin, bool large) const
{
    const qreal factor = large ? 0.30 : (thin ? 0.20 : 0.25);
    return headLen * factor;
}

void LineShape::drawArrowHead(QPainter *p, const QPointF &from, const QPointF &tip,
                              bool thin, bool large) const
{
    const QLineF line(from, tip);
    const qreal len = line.length();
    if (len < 1.0)
        return;

    const qreal headLen = arrowHeadLength(thin, large);
    const qreal headHalfWidth = arrowHeadHalfWidth(headLen, thin, large);

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


bool LineShape::isTaperedStyle() const
{
    return m_arrowStyle == ArrowStyle::Tapered
           || m_arrowStyle == ArrowStyle::TaperedThin
           || m_arrowStyle == ArrowStyle::DeepBarb;
}

// 锥形倒刺箭头：整条箭头是**一个闭合填充路径**，不用等宽 stroke。
//   起点：半宽≈0.5px 的平头（几乎为 0）
//   线身：沿方向逐渐变宽（锥形）
//   箭头区：先向外张开成倒刺尖，再向内收缩到尖端
// 参数都是笔宽倍数，保证任何笔宽下比例一致、大小随笔宽缩放。
void LineShape::drawTaperedArrow(QPainter *p, const QVector<QPointF> &pts) const
{
    if (pts.size() < 2)
        return;

    // 三档的差异只在比例上
    qreal headLenF, barbHalfF, shaftHalfF, barbBackF;
    if (m_arrowStyle == ArrowStyle::TaperedThin) {
        headLenF = 4.0;  barbHalfF = 1.45; shaftHalfF = 0.62; barbBackF = 0.75;
    } else if (m_arrowStyle == ArrowStyle::DeepBarb) {
        headLenF = 3.4;  barbHalfF = 1.80; shaftHalfF = 0.70; barbBackF = 0.55;
    } else {   // Tapered 标准
        headLenF = 3.2;  barbHalfF = 1.70; shaftHalfF = 0.75; barbBackF = 0.90;
    }

    const qreal w = qMax<qreal>(1.0, m_penWidth);

    // ---- 箭头区（从最后一个点往前量）：三点定位 ----
    const QPointF tip = pts.last();
    QPointF back = pts.at(pts.size() - 2);          // 指向 tip 的方向
    QLineF axis(back, tip);
    qreal axisLen = axis.length();
    if (axisLen < 0.5)
        return;

    const qreal headLen = headLenF * w;
    const qreal barbHalf = barbHalfF * w;
    const qreal shaftHalf = shaftHalfF * w;
    const qreal barbBack = barbBackF * w;

    // 线太短时整体按比例缩小，避免箭头比线还长
    const qreal shrink = qMin<qreal>(1.0, axisLen / (headLen + 1.0));
    const qreal hLen = headLen * shrink;
    const qreal bh = barbHalf * shrink;
    const qreal sh = shaftHalf * shrink;
    const qreal bb = barbBack * shrink;

    const QPointF dir = (tip - back) / axisLen;
    const QPointF normal(-dir.y(), dir.x());

    const QPointF barbTip = tip - dir * hLen;        // 倒刺尖的 x 位置
    const QPointF shaftEnd = barbTip + dir * bb;     // 笔杆结束（倒刺后伸）
    const QPointF start = pts.first();

    // 起点：半宽≈0.5px 的平头
    const qreal startHalf = 0.5;

    // ---- 线身：起点→笔杆结束 用二次曲线渐变宽度 ----
    // 用采样点沿轴插值宽度，保证是"锥形"而不是两段直线
    constexpr int kSamples = 24;
    QVector<QPointF> upper, lower;
    upper.reserve(kSamples + 4);
    lower.reserve(kSamples + 4);

    for (int i = 0; i <= kSamples; ++i) {
        const qreal t = qreal(i) / kSamples;
        const QPointF center = start + (shaftEnd - start) * t;

        // 宽度曲线：ease-in，越到箭头越粗（锥形收锋感）
        const qreal k = t * t * (3.0 - 2.0 * t);   // smoothstep
        const qreal half = startHalf + (sh - startHalf) * k;

        upper.append(center + normal * half);
        lower.append(center - normal * half);
    }

    // ---- 组装闭合路径 ----
    QPainterPath path;
    path.moveTo(upper.first());
    for (int i = 1; i < upper.size(); ++i)
        path.lineTo(upper.at(i));

    path.lineTo(barbTip + normal * bh);   // 上侧倒刺尖
    path.lineTo(tip);                     // 尖端
    path.lineTo(barbTip - normal * bh);   // 下侧倒刺尖

    for (int i = lower.size() - 1; i >= 0; --i)
        path.lineTo(lower.at(i));

    path.closeSubpath();

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(m_color);
    p->drawPath(path);
    p->restore();
}

void LineShape::paintContent(QPainter *p) const
{
    if (m_points.size() < 2)
        return;

    const bool isArrow = (m_kind == Kind::Arrow);
    const bool doubleHeaded = m_arrowStyle == ArrowStyle::Double;

    // 锥形倒刺档位：整条箭头就是一个闭合填充路径，不走"笔杆+三角"两段绘制
    if (isArrow && isTaperedStyle()) {
        drawTaperedArrow(p, m_points);
        return;
    }

    const bool thin = false;    // 旧档位（单头/双头）只有常规比例
    const bool large = false;

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

    // 等宽笔杆（普通直线/折线，以及单头/双头箭头的线身）
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
