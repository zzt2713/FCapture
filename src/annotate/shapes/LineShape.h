#pragma once

#include "annotate/Annotation.h"

// 直线 / 折线 / 箭头
class LineShape : public Annotation
{
public:
    enum class Kind { Line, Polyline, Arrow };

    // 箭头样式：单头 / 双头（实心三角） + 标准 / 细长 / 深倒刺（锥形倒刺）
    enum class ArrowStyle {
        Single,       // 1 单头（实心三角，等宽笔杆）
        Double,       // 2 双头（实心三角）
        Tapered,      // 3 标准锥形倒刺
        TaperedThin,  // 4 细长锥形倒刺
        DeepBarb      // 5 深倒刺
    };

    explicit LineShape(Kind kind);

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

    // 线/折线不可旋转
    bool isRotatable() const override { return false; }

    void setPoints(const QVector<QPointF> &pts);
    void appendPoint(const QPointF &pt);
    void setLastPoint(const QPointF &pt);
    QVector<QPointF> points() const { return m_points; }

    void setKind(Kind k) { m_kind = k; }
    Kind kind() const { return m_kind; }

    void setArrowStyle(ArrowStyle s) { m_arrowStyle = s; }
    ArrowStyle arrowStyle() const { return m_arrowStyle; }

    void setPenStyle(Qt::PenStyle s) { m_penStyle = s; }
    Qt::PenStyle penStyle() const { return m_penStyle; }

private:
    qreal arrowHeadLength(bool thin, bool large) const;
    qreal arrowHeadHalfWidth(qreal headLen, bool thin, bool large) const;
    void drawArrowHead(QPainter *p, const QPointF &from, const QPointF &tip, bool thin, bool large) const;

    // 锥形倒刺箭头：整条箭头是一个闭合填充路径
    // （起点≈1px 平头 → 线身渐粗 → 张成倒刺 → 收成尖），不用等宽 stroke
    bool isTaperedStyle() const;
    void drawTaperedArrow(QPainter *p, const QVector<QPointF> &pts) const;

    Kind m_kind;
    QVector<QPointF> m_points;
    ArrowStyle m_arrowStyle = ArrowStyle::Single;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
};
