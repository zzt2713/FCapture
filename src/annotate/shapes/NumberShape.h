#pragma once

#include "annotate/Annotation.h"

// 序号标注：实心圆 + 白色数字
class NumberShape : public Annotation
{
public:
    NumberShape();

    // 序号计数：按分组独立递增
    static int  nextNumber(int groupId = 0);
    static int  peekNumber(int groupId = 0);
    static void resetNumbering(int groupId = 0);
    static void resetAllNumbering();

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translateContent(const QPointF &delta) override;
    void scaleAbout(const QPointF &center, qreal factor) override;
    QVector<QPointF> rawHandles() const override;
    void moveHandleRaw(int index, const QPointF &pos) override;
    QJsonObject toJson() const override;
    std::shared_ptr<Annotation> clone() const override;

    int  number() const { return m_number; }
    void setNumber(int n) { m_number = qMax(1, n); }
    int  groupId() const { return m_groupId; }
    void setGroupId(int g) { m_groupId = g; }

    void setCenter(const QPointF &c) { m_center = c; }
    QPointF center() const { return m_center; }

    qreal radius() const;

private:
    int     m_number = 1;
    int     m_groupId = 0;
    QPointF m_center;
};
