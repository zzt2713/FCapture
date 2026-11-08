#pragma once

#include "annotate/Annotation.h"

#include <QImage>
#include <QPainterPath>

// 马赛克 / 高斯模糊 / 橡皮擦
// 三者都是"对底图做像素级操作"，共用同一套实现，只是滤镜不同
class PixelShape : public Annotation
{
public:
    enum class Kind { Mosaic, Blur, Eraser };
    enum class Mode { Brush, Rect };   // 涂抹 / 矩形选区

    explicit PixelShape(Kind kind);

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }
    void setRect(const QRectF &r);

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translate(const QPointF &delta) override;
    void scaleAbout(const QPointF &center, qreal factor) override;
    QVector<QPointF> rawHandles() const override;
    void moveHandleRaw(int index, const QPointF &pos) override;
    void invalidateCache() override;
    QJsonObject toJson() const override;
    std::shared_ptr<Annotation> clone() const override;

    // 像素类标注不可旋转（滤镜按源图像素块计算，旋转会破坏对齐）
    bool isRotatable() const override { return false; }

    void appendPoint(const QPointF &pt);
    void setPoints(const QVector<QPointF> &pts);
    QVector<QPointF> points() const { return m_points; }

    void setEllipseBrush(bool on) { m_ellipseBrush = on; }
    bool ellipseBrush() const { return m_ellipseBrush; }

    // 需要有底图才能计算；由 SnipOverlay 在绘制前注入
    static void setSourceImage(const QImage &img, qreal devicePixelRatio);
    static void clearSourceImage();

    // 本形状影响的矩形区域（用于局部刷新）
    QRectF affectedRect() const;

private:
    void ensureCache() const;
    QPainterPath strokePath() const;

    Kind m_kind;
    Mode m_mode = Mode::Brush;
    QRectF m_rect;
    QVector<QPointF> m_points;
    bool m_ellipseBrush = false;

    mutable QImage  m_cached;       // 滤镜结果，坐标同源图
    mutable QRect   m_cachedRect;   // 结果覆盖的源图区域
    mutable bool    m_cacheValid = false;

    static QImage s_source;
    static qreal  s_sourceDpr;
};
