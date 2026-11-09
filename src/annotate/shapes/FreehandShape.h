#pragma once

#include "annotate/Annotation.h"

#include <QPainterPath>

// 铅笔 / 马克笔：都是自由曲线，差别在笔宽倍数和透明度
class FreehandShape : public Annotation
{
public:
    enum class Kind { Pencil, Marker };

    explicit FreehandShape(Kind kind);

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translate(const QPointF &delta) override;
    void scaleAbout(const QPointF &center, qreal factor) override;
    QVector<QPointF> rawHandles() const override;
    void moveHandleRaw(int index, const QPointF &pos) override;
    QJsonObject toJson() const override;
    std::shared_ptr<Annotation> clone() const override;

    void appendPoint(const QPointF &pt);
    void setPoints(const QVector<QPointF> &pts) { m_points = pts; m_pathDirty = true; }
    QVector<QPointF> points() const { return m_points; }

    void setPenStyle(Qt::PenStyle s) { m_penStyle = s; }
    Qt::PenStyle penStyle() const { return m_penStyle; }

    // 椭圆笔头：笔画截面为椭圆（铅笔/马克笔均支持）
    void setEllipseBrush(bool on) { m_ellipseBrush = on; }
    bool ellipseBrush() const { return m_ellipseBrush; }

private:
    QPainterPath buildPath() const;
    mutable QPainterPath m_pathCache;
    mutable bool m_pathDirty = true;

    Kind m_kind;
    QVector<QPointF> m_points;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
    bool m_ellipseBrush = false;
};
