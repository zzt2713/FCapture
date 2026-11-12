#pragma once

#include "annotate/Annotation.h"

#include <QImage>

// 放大镜标注：把截图上的某块区域放大后画到另一个位置，并画出源框和引导线
class MagnifyShape : public Annotation
{
public:
    MagnifyShape();

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

    // 第一阶段：确定源区域，同时从快照裁出该区域（物理像素）
    void setSourceRect(const QRectF &rect, const QImage &snapshot, qreal dpr, const QPointF &origin);

    // 拖拽过程中的轻量更新：只改框，不裁片（裁片留到松手时做一次，
    // 否则每帧都 copy 一大块图，大区域拖动会明显掉帧）
    void setSourceRectPreview(const QRectF &rect);
    // 用最近一次快照重新裁片（拖拽结束后调用）
    void captureSource();

    QRectF sourceRect() const { return m_sourceRect; }

    // 第二阶段：确定绘制目标区域
    void setTargetRect(const QRectF &rect) { m_targetRect = rect.normalized(); }
    QRectF targetRect() const { return m_targetRect; }

    // 控制点布局：0~7 为目标框 8 个锚点，8~11 为源框 4 个角
    static constexpr int kTargetHandleCount = 8;
    static constexpr int kSourceHandleStart = 8;

    bool hasSource() const { return !m_sourcePatch.isNull(); }
    bool hasTarget() const { return m_targetRect.width() >= 1 && m_targetRect.height() >= 1; }

    qreal factor() const;
    void setFactor(qreal f) { m_factor = qBound<qreal>(1.0, f, 8.0); }

    void setShowBorder(bool on) { m_showBorder = on; }
    bool showBorder() const { return m_showBorder; }

private:
    // 源框被拖动后重新从整张快照裁片（需要快照本身）
    void recaptureSource(const QRectF &newRect);

    QRectF m_sourceRect;     // 逻辑坐标（截图层坐标系）
    QRectF m_targetRect;     // 逻辑坐标
    QImage m_sourcePatch;    // 源区域裁片，物理像素
    QImage m_snapshot;       // 完整快照，仅用于拖动源框时重新裁片
    qreal  m_snapshotDpr = 1.0;
    QPointF m_origin;        // 截图层局部原点在快照中的偏移（通常为 0）
    qreal  m_factor = 2.0;
    bool   m_showBorder = true;
};
