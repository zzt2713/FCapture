#include "annotate/shapes/RectShapes.h"

#include <QJsonObject>

RectShape::RectShape(Form form)
    : m_form(form)
{
}

Annotation::Type RectShape::type() const
{
    return m_form == Form::Rectangle ? Type::Rectangle : Type::Ellipse;
}

qreal RectShape::effectiveCornerRadius() const
{
    return qBound<qreal>(0, m_cornerRadius, qMin(m_rect.width(), m_rect.height()) / 2.0);
}

QRectF RectShape::rawBoundingRect() const
{
    const qreal half = m_penWidth / 2.0 + 1.0;
    return m_rect.adjusted(-half, -half, half, half);
}

void RectShape::paintContent(QPainter *p) const
{
    if (m_rect.width() < 1 || m_rect.height() < 1)
        return;

    p->setPen(QPen(m_color, m_penWidth, m_penStyle, Qt::SquareCap, Qt::MiterJoin));
    // 实心：内部填同色；描边仍保留，边缘更干净
    p->setBrush(m_filled ? QBrush(m_color) : QBrush(Qt::NoBrush));

    // 内缩半个笔宽，避免描边被选区边界裁掉一半
    const QRectF r = m_rect.adjusted(m_penWidth / 2.0, m_penWidth / 2.0,
                                     -m_penWidth / 2.0, -m_penWidth / 2.0);

    if (m_form == Form::Ellipse) {
        p->drawEllipse(r);
        return;
    }

    const qreal radius = effectiveCornerRadius();
    if (radius > 0.5)
        p->drawRoundedRect(r, radius, radius);
    else
        p->drawRect(r);
}

bool RectShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    const QRectF outer = rawBoundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (!outer.contains(pt))
        return false;

    // 实心图形整块都能点中；空心图形只命中边框，不命中内部空白
    if (m_filled)
        return true;

    const QRectF inner = m_rect.adjusted(tolerance + 2, tolerance + 2,
                                         -tolerance - 2, -tolerance - 2);
    return !inner.contains(pt);
}

void RectShape::translate(const QPointF &delta)
{
    m_rect.translate(delta);
}

void RectShape::scaleAbout(const QPointF &center, qreal factor)
{
    const QPointF tl = center + (m_rect.topLeft() - center) * factor;
    const QPointF br = center + (m_rect.bottomRight() - center) * factor;
    m_rect = QRectF(tl, br).normalized();
    m_cornerRadius *= factor;
}

QVector<QPointF> RectShape::rawHandles() const
{
    const QRectF r = m_rect;
    return {
        r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(),
        QPointF(r.right(), r.center().y()), r.bottomRight(),
        QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
        QPointF(r.left(), r.center().y())
    };
}

void RectShape::moveHandleRaw(int index, const QPointF &pos)
{
    // 用「对边」重建矩形，而不是 setLeft/setRight。
    //
    // 关键：对边不是固定那一条，要按**鼠标当前落在哪一侧**来选。
    // 拖左边一路往右越过右边之后，实际掌管的是"鼠标左侧那条边"，
    // 若继续拿旧 right 当对边，宽度会被一直压成 0（长条 / 右边一路跑）。
    QRectF r = m_rect;
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();

    const bool dragsLeft = pos.x() < cx;   // 鼠标在中心左侧 → 拖的是左边界
    const bool dragsTop = pos.y() < cy;    // 鼠标在中心上方 → 拖的是上边界

    // 固定边：与拖动边相对的那一侧
    const qreal fixedX = dragsLeft ? r.right() : r.left();
    const qreal fixedY = dragsTop ? r.bottom() : r.top();

    switch (index) {
    // 四个角
    case 0: r = QRectF(pos, QPointF(fixedX, fixedY)).normalized(); break;
    case 2: r = QRectF(QPointF(fixedX, pos.y()), QPointF(pos.x(), fixedY)).normalized(); break;
    case 4: r = QRectF(QPointF(fixedX, fixedY), pos).normalized(); break;
    case 6: r = QRectF(QPointF(pos.x(), fixedY), QPointF(fixedX, pos.y())).normalized(); break;
    // 四条边（只影响一个轴）
    case 1: r = QRectF(QPointF(r.left(), pos.y()),
                       QPointF(r.right(), fixedY)).normalized(); break;
    case 5: r = QRectF(QPointF(r.left(), fixedY),
                       QPointF(r.right(), pos.y())).normalized(); break;
    case 3: r = QRectF(QPointF(fixedX, r.top()),
                       QPointF(pos.x(), r.bottom())).normalized(); break;
    case 7: r = QRectF(QPointF(pos.x(), r.top()),
                       QPointF(fixedX, r.bottom())).normalized(); break;
    default: return;
    }

    // 始终接受（哪怕瞬间宽/高为 0），否则会卡在退化帧导致后续拖拽失效
    m_rect = r;
}

void RectShape::moveHandleKeepAspect(int index, const QPointF &pos)
{
    // 角手柄 + Shift：等比。以对角为锚点，按较大的边算比例，另一条边跟着走。
    const QRectF r = m_rect;
    const bool west = (index == 0 || index == 6);   // 拖的是左边
    const bool north = (index == 0 || index == 2);  // 拖的是上边

    const QPointF anchor(west ? r.right() : r.left(),
                         north ? r.bottom() : r.top());
    const qreal w = qAbs(pos.x() - anchor.x());
    const qreal h = qAbs(pos.y() - anchor.y());
    if (w < 2 || h < 2)
        return;

    const qreal ratio = r.height() / qMax<qreal>(1.0, r.width());
    qreal newW = w;
    qreal newH = newW * ratio;
    if (newH < h) {
        newH = h;
        newW = newH / qMax(0.0001, ratio);
    }

    const qreal x = west ? anchor.x() - newW : anchor.x();
    const qreal y = north ? anchor.y() - newH : anchor.y();
    m_rect = QRectF(QPointF(x, y), QSizeF(newW, newH)).normalized();
}

QJsonObject RectShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = m_form == Form::Rectangle ? QStringLiteral("rect")
                                                          : QStringLiteral("ellipse");
    o[QStringLiteral("x")] = m_rect.x();
    o[QStringLiteral("y")] = m_rect.y();
    o[QStringLiteral("w")] = m_rect.width();
    o[QStringLiteral("h")] = m_rect.height();
    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("penStyle")] = int(m_penStyle);
    o[QStringLiteral("cornerRadius")] = m_cornerRadius;
    o[QStringLiteral("filled")] = m_filled;
    o[QStringLiteral("rotation")] = m_rotation;
    return o;
}

std::shared_ptr<Annotation> RectShape::clone() const
{
    auto copy = std::make_shared<RectShape>(m_form);
    copy->m_rect = m_rect;
    copy->m_color = m_color;
    copy->m_penWidth = m_penWidth;
    copy->m_cornerRadius = m_cornerRadius;
    copy->m_penStyle = m_penStyle;
    copy->m_filled = m_filled;
    copy->m_rotation = m_rotation;
    return copy;
}
