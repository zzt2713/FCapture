#include "annotate/Annotation.h"

#include <QPolygonF>

#include <cmath>

namespace {

// 绕 center 旋转 deg 度的变换
QTransform aroundCenter(const QPointF &center, qreal deg)
{
    return QTransform().translate(center.x(), center.y())
        .rotate(deg)
        .translate(-center.x(), -center.y());
}

} // namespace

QPointF Annotation::rotationAnchor() const
{
    // 永远取当前中心：几何一变（拖手柄/缩放/平移），支点自动跟上
    return rawBoundingRect().center();
}

QTransform Annotation::rotationTransform() const
{
    if (qFuzzyIsNull(m_rotation))
        return QTransform();
    return aroundCenter(rotationAnchor(), m_rotation);
}

void Annotation::setRotation(qreal degrees)
{

    // 归一化到 [0, 360)，避免长按累积成很大的数
    m_rotation = std::fmod(degrees, 360.0);
    if (m_rotation < 0)
        m_rotation += 360.0;
}

void Annotation::paint(QPainter *p) const
{
    if (qFuzzyIsNull(m_rotation)) {
        paintContent(p);
        return;
    }

    p->save();
    p->setTransform(rotationTransform(), true);
    paintContent(p);
    p->restore();
}

QRectF Annotation::boundingRect() const
{
    const QRectF raw = rawBoundingRect();
    if (qFuzzyIsNull(m_rotation))
        return raw;

    QPolygonF poly;
    poly << raw.topLeft() << raw.topRight() << raw.bottomRight() << raw.bottomLeft();
    return rotationTransform().map(poly).boundingRect();
}

bool Annotation::hitTest(const QPointF &pt, qreal tolerance) const
{
    QPointF local = pt;
    if (!qFuzzyIsNull(m_rotation))
        local = rotationTransform().inverted().map(pt);
    return hitTestContent(local, tolerance);
}

QVector<QPointF> Annotation::handlePositions() const
{
    QVector<QPointF> handles = rawHandles();
    const QTransform t = rotationTransform();
    for (QPointF &h : handles)
        h = t.map(h);

    // 旋转手柄：包围盒顶边中点上方 24px（随图形一起旋转）
    if (isRotatable()) {
        const QRectF raw = rawBoundingRect();
        handles.append(t.map(QPointF(raw.center().x(), raw.top() - 24.0)));
    }
    return handles;
}

void Annotation::moveHandle(int index, const QPointF &pos)
{
    // 旋转手柄不改变几何数据，由调用方（SnipOverlay）负责算角度
    if (index < 0 || index >= rawHandles().size())
        return;

    QPointF local = pos;
    if (!qFuzzyIsNull(m_rotation))
        local = rotationTransform().inverted().map(pos);
    moveHandleRaw(index, local);
}
