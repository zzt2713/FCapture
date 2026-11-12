#pragma once

#include "annotate/Annotation.h"

// 矩形 / 椭圆：共用一套实现，只是绘制方式不同
class RectShape : public Annotation
{
public:
    enum class Form { Rectangle, Ellipse };

    explicit RectShape(Form form);

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translateContent(const QPointF &delta) override;
    void scaleAbout(const QPointF &center, qreal factor) override;
    QVector<QPointF> rawHandles() const override;
    void moveHandleRaw(int index, const QPointF &pos) override;
    // Shift + 拖角：保持宽高比
    void moveHandleKeepAspect(int index, const QPointF &pos);
    QJsonObject toJson() const override;
    std::shared_ptr<Annotation> clone() const override;

    void setRect(const QRectF &r) { m_rect = r.normalized(); }
    QRectF rect() const { return m_rect; }

    // 圆角半径（仅矩形生效，0 = 直角）
    void setCornerRadius(qreal r) { m_cornerRadius = qMax<qreal>(0, r); }
    qreal cornerRadius() const { return m_cornerRadius; }

    void setPenStyle(Qt::PenStyle s) { m_penStyle = s; }
    Qt::PenStyle penStyle() const { return m_penStyle; }

    // 实心：内部用同色填充
    void setFilled(bool on) { m_filled = on; }
    bool isFilled() const { return m_filled; }

    // 圆角半径上限：不超过短边一半
    qreal effectiveCornerRadius() const;

private:
    Form   m_form;
    QRectF m_rect;
    qreal  m_cornerRadius = 0;
    bool   m_filled = false;
    Qt::PenStyle m_penStyle = Qt::SolidLine;
};
