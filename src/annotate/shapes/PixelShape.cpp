#include "annotate/shapes/PixelShape.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>

namespace {

// 像素化：按 block 大小取块内平均色
QImage applyMosaic(const QImage &src, int block)
{
    if (src.isNull() || block < 2)
        return src;

    QImage out = src.convertToFormat(QImage::Format_ARGB32);
    const int w = out.width();
    const int h = out.height();

    for (int by = 0; by < h; by += block) {
        for (int bx = 0; bx < w; bx += block) {
            const int bw = qMin(block, w - bx);
            const int bh = qMin(block, h - by);

            qint64 r = 0, g = 0, b = 0, a = 0;
            int n = 0;
            for (int y = by; y < by + bh; ++y) {
                const auto *line = reinterpret_cast<const QRgb *>(out.constScanLine(y));
                for (int x = bx; x < bx + bw; ++x) {
                    const QRgb px = line[x];
                    r += qRed(px);
                    g += qGreen(px);
                    b += qBlue(px);
                    a += qAlpha(px);
                    ++n;
                }
            }
            if (n == 0)
                continue;

            const QRgb avg = qRgba(int(r / n), int(g / n), int(b / n), int(a / n));
            for (int y = by; y < by + bh; ++y) {
                auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
                for (int x = bx; x < bx + bw; ++x)
                    line[x] = avg;
            }
        }
    }
    return out;
}

// 单方向滑动窗口盒式模糊，O(w*h)。
// horizontal=true 沿 x 方向，否则沿 y 方向。
void boxBlurPass(const QImage &src, QImage &dst, int radius, bool horizontal)
{
    const int w = src.width();
    const int h = src.height();
    const int r = qMax(1, radius);
    const int span = r * 2 + 1;

    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;

    for (int o = 0; o < outer; ++o) {
        // 按 inner 索引取像素，越界时 clamp 到边缘
        auto at = [&](int idx) -> QRgb {
            const int c = qBound(0, idx, inner - 1);
            if (horizontal)
                return reinterpret_cast<const QRgb *>(src.constScanLine(o))[c];
            return reinterpret_cast<const QRgb *>(src.constScanLine(c))[o];
        };
        auto store = [&](int idx, QRgb v) {
            if (horizontal)
                reinterpret_cast<QRgb *>(dst.scanLine(o))[idx] = v;
            else
                reinterpret_cast<QRgb *>(dst.scanLine(idx))[o] = v;
        };

        qint64 sr = 0, sg = 0, sb = 0, sa = 0;
        for (int k = -r; k <= r; ++k) {
            const QRgb px = at(k);
            sr += qRed(px);
            sg += qGreen(px);
            sb += qBlue(px);
            sa += qAlpha(px);
        }

        for (int i = 0; i < inner; ++i) {
            store(i, qRgba(int(sr / span), int(sg / span), int(sb / span), int(sa / span)));

            const QRgb outPx = at(i - r);
            const QRgb inPx = at(i + r + 1);
            sr += qRed(inPx) - qRed(outPx);
            sg += qGreen(inPx) - qGreen(outPx);
            sb += qBlue(inPx) - qBlue(outPx);
            sa += qAlpha(inPx) - qAlpha(outPx);
        }
    }
}

// 盒式模糊，两遍近似高斯
QImage applyBoxBlur(const QImage &src, int radius)
{
    if (src.isNull() || radius < 1)
        return src;

    QImage a = src.convertToFormat(QImage::Format_ARGB32);
    QImage b(a.size(), QImage::Format_ARGB32);

    const int r = qMax(1, radius);
    boxBlurPass(a, b, r, true);
    boxBlurPass(b, a, r, false);
    // 第二遍让结果更平滑
    boxBlurPass(a, b, r, true);
    boxBlurPass(b, a, r, false);

    return a;
}

} // namespace

QImage PixelShape::s_source;
qreal PixelShape::s_sourceDpr = 1.0;

PixelShape::PixelShape(Kind kind)
    : m_kind(kind)
{
    if (kind == Kind::Eraser) {
        m_penWidth = 20;
        m_color = QColor(0, 0, 0, 255);
    } else if (kind == Kind::Blur) {
        m_penWidth = 12;
        m_color = QColor(0x3D, 0x8B, 0xFD);
    } else {
        m_penWidth = 12;
        m_color = QColor(0x3D, 0x8B, 0xFD);
    }
}

Annotation::Type PixelShape::type() const
{
    switch (m_kind) {
    case Kind::Mosaic: return Type::Mosaic;
    case Kind::Blur:   return Type::Blur;
    case Kind::Eraser: return Type::Eraser;
    }
    return Type::Mosaic;
}

void PixelShape::setSourceImage(const QImage &img, qreal devicePixelRatio)
{
    s_source = img;
    s_sourceDpr = devicePixelRatio > 0 ? devicePixelRatio : 1.0;
}

void PixelShape::clearSourceImage()
{
    s_source = QImage();
    s_sourceDpr = 1.0;
}

void PixelShape::appendPoint(const QPointF &pt)
{
    if (!m_points.isEmpty() && QLineF(m_points.last(), pt).length() < 1.5)
        return;
    m_points.append(pt);
    m_cacheValid = false;
}

void PixelShape::setPoints(const QVector<QPointF> &pts)
{
    m_points = pts;
    m_cacheValid = false;
}

void PixelShape::invalidateCache()
{
    m_cacheValid = false;
}

void PixelShape::setRect(const QRectF &r)
{
    m_rect = r.normalized();
    m_cacheValid = false;
}

QPainterPath PixelShape::strokePath() const
{
    // 矩形模式：直接返回矩形路径
    if (m_mode == Mode::Rect) {
        QPainterPath path;
        if (m_rect.width() >= 1 && m_rect.height() >= 1)
            path.addRect(m_rect);
        return path;
    }

    QPainterPath centerLine;
    if (m_points.isEmpty())
        return centerLine;

    centerLine.moveTo(m_points.first());
    if (m_points.size() == 1)
        centerLine.lineTo(m_points.first() + QPointF(0.01, 0.01));
    for (int i = 1; i < m_points.size(); ++i)
        centerLine.lineTo(m_points.at(i));

    if (m_ellipseBrush) {
        // 椭圆笔头：沿路径盖固定朝向的椭圆，合并成连续条纹
        QPainterPath stamps;
        stamps.setFillRule(Qt::WindingFill);
        const qreal rx = m_penWidth * 0.95;
        const qreal ry = m_penWidth * 0.5;
        const qreal step = qMax<qreal>(1.0, m_penWidth / 4.0);
        for (int i = 1; i < m_points.size(); ++i) {
            const QLineF seg(m_points.at(i - 1), m_points.at(i));
            const int n = qMax(1, int(seg.length() / step));
            for (int k = 0; k <= n; ++k) {
                const QPointF pt = seg.pointAt(qreal(k) / n);
                stamps.addEllipse(QRectF(pt.x() - rx, pt.y() - ry, rx * 2, ry * 2));
            }
        }
        return stamps;
    }

    QPainterPathStroker stroker;
    stroker.setWidth(m_penWidth);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(centerLine);
}

QRectF PixelShape::affectedRect() const
{
    if (m_mode == Mode::Rect)
        return m_rect;
    if (m_points.isEmpty())
        return {};
    return strokePath().boundingRect();
}

QRectF PixelShape::rawBoundingRect() const
{
    return affectedRect();
}

void PixelShape::translate(const QPointF &delta)
{
    if (m_mode == Mode::Rect)
        m_rect.translate(delta);
    else
        for (QPointF &pt : m_points)
            pt += delta;
    m_cacheValid = false;
}

void PixelShape::scaleAbout(const QPointF &center, qreal factor)
{
    if (m_mode == Mode::Rect) {
        const QPointF tl = center + (m_rect.topLeft() - center) * factor;
        const QPointF br = center + (m_rect.bottomRight() - center) * factor;
        m_rect = QRectF(tl, br).normalized();
    } else {
        for (QPointF &pt : m_points)
            pt = center + (pt - center) * factor;
    }
    m_penWidth = qMax(1, qRound(m_penWidth * factor));
    m_cacheValid = false;
}

QVector<QPointF> PixelShape::rawHandles() const
{
    if (m_mode == Mode::Rect) {
        const QRectF r = m_rect;
        if (r.isEmpty())
            return {};
        return {
            r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(),
            QPointF(r.right(), r.center().y()), r.bottomRight(),
            QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
            QPointF(r.left(), r.center().y())
        };
    }
    if (m_points.size() < 2)
        return {};
    return {m_points.first(), m_points.last()};
}

void PixelShape::moveHandleRaw(int index, const QPointF &pos)
{
    if (m_mode == Mode::Rect) {
        // 对边固定重建矩形：拖过对边时锚点翻转，矩形继续跟着鼠标长
        //（不能用 setLeft/setRight —— 越过对边后 normalize 会让对边"往回跑"）
        QRectF r = m_rect;
        const qreal left = r.left();
        const qreal right = r.right();
        const qreal top = r.top();
        const qreal bottom = r.bottom();
        switch (index) {
        case 0: r = QRectF(pos, QPointF(right, bottom)).normalized(); break;
        case 1: r = QRectF(QPointF(left, pos.y()), QPointF(right, bottom)).normalized(); break;
        case 2: r = QRectF(QPointF(left, pos.y()), QPointF(pos.x(), bottom)).normalized(); break;
        case 3: r = QRectF(QPointF(left, top), QPointF(pos.x(), bottom)).normalized(); break;
        case 4: r = QRectF(QPointF(left, top), pos).normalized(); break;
        case 5: r = QRectF(QPointF(left, top), QPointF(right, pos.y())).normalized(); break;
        case 6: r = QRectF(QPointF(pos.x(), top), QPointF(right, bottom)).normalized(); break;
        case 7: r = QRectF(QPointF(pos.x(), top), QPointF(right, bottom)).normalized(); break;
        default: return;
        }
        // 不要因尺寸过小而丢弃更新：拖过对边时宽度会瞬间归零，
        // 丢弃会让矩形卡在退化状态（表现为"变成一条长条"）
        m_rect = r;
    } else if (index == 0 && !m_points.isEmpty()) {
        m_points.first() = pos;
    } else if (index == 1 && m_points.size() >= 2) {
        m_points.last() = pos;
    }
    m_cacheValid = false;
}

void PixelShape::ensureCache() const
{
    // 橡皮擦不做滤镜缓存（它只对图层做 Clear）
    if (m_kind == Kind::Eraser)
        return;

    if (m_cacheValid || s_source.isNull())
        return;

    // 注意：不能判断 m_points.isEmpty()——矩形模式没有 points，
    // 之前这样写导致矩形模式的马赛克/模糊永远算不出缓存、绘制为空。
    const qreal dpr = s_sourceDpr;
    const QRectF logicalArea = affectedRect();
    if (logicalArea.isEmpty())
        return;

    // 逻辑 → 源图物理像素
    QRect physArea(qRound(logicalArea.x() * dpr), qRound(logicalArea.y() * dpr),
                   qRound(logicalArea.width() * dpr), qRound(logicalArea.height() * dpr));
    physArea = physArea.intersected(s_source.rect());
    if (physArea.isEmpty())
        return;

    const QImage patch = s_source.copy(physArea);
    QImage filtered;

    switch (m_kind) {
    case Kind::Mosaic: {
        // 块大小约等于笔宽的一半，视觉上明显但不至于糊成一坨
        const int block = qMax(3, qRound(m_penWidth * dpr * 0.45));
        filtered = applyMosaic(patch, block);
        break;
    }
    case Kind::Blur: {
        const int radius = qMax(1, qRound(m_penWidth * dpr * 0.35));
        filtered = applyBoxBlur(patch, radius);
        break;
    }
    case Kind::Eraser:
        filtered = patch;
        break;
    }

    m_cached = filtered;
    m_cachedRect = physArea;
    m_cacheValid = true;
}

// 缓存失效由 appendPoint / setPoints / translate 触发

void PixelShape::paintContent(QPainter *p) const
{
    // 矩形模式没有 points，不能在这里直接返回（曾导致矩形模式完全无效）
    if (m_mode == Mode::Brush && m_points.isEmpty())
        return;
    if (m_mode == Mode::Rect && (m_rect.width() < 1 || m_rect.height() < 1))
        return;

    ensureCache();

    const qreal dpr = s_sourceDpr;
    const QPainterPath clipPath = strokePath();

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    if (m_kind == Kind::Eraser) {
        // 橡皮擦只擦除"自己画的标注"。
        // AnnotationModel::render 在有橡皮擦时会把所有标注画到独立透明层，
        // 这里 Clear 掉的是那一层上的标注像素，底图不受影响。
        p->setCompositionMode(QPainter::CompositionMode_Clear);
        p->setPen(Qt::NoPen);
        p->setBrush(Qt::black);
        p->drawPath(clipPath);
        p->restore();
        return;
    }

    if (m_cached.isNull()) {
        p->restore();
        return;
    }

    p->setClipPath(clipPath);

    // 缓存在源图像素空间，绘制时换回逻辑坐标
    const QRectF targetRect(m_cachedRect.x() / dpr, m_cachedRect.y() / dpr,
                            m_cachedRect.width() / dpr, m_cachedRect.height() / dpr);
    p->drawImage(targetRect, m_cached);

    p->restore();
}

bool PixelShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    if (m_mode == Mode::Brush && m_points.isEmpty())
        return false;
    if (m_mode == Mode::Rect && m_rect.isEmpty())
        return false;
    const QPainterPath path = strokePath();
    return path.contains(pt)
           || path.boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(pt);
}

QJsonObject PixelShape::toJson() const
{
    QJsonObject o;
    QString t;
    switch (m_kind) {
    case Kind::Mosaic: t = QStringLiteral("mosaic"); break;
    case Kind::Blur:   t = QStringLiteral("blur"); break;
    case Kind::Eraser: t = QStringLiteral("eraser"); break;
    }
    o[QStringLiteral("type")] = t;

    QJsonArray pts;
    for (const QPointF &pt : m_points)
        pts.append(QJsonArray{pt.x(), pt.y()});
    o[QStringLiteral("points")] = pts;
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("mode")] = int(m_mode);
    o[QStringLiteral("ellipseBrush")] = m_ellipseBrush;
    o[QStringLiteral("rect")] = QJsonArray{m_rect.x(), m_rect.y(), m_rect.width(), m_rect.height()};
    return o;
}

std::shared_ptr<Annotation> PixelShape::clone() const
{
    auto copy = std::make_shared<PixelShape>(m_kind);
    copy->m_points = m_points;
    copy->m_penWidth = m_penWidth;
    copy->m_color = m_color;
    copy->m_mode = m_mode;
    copy->m_rect = m_rect;
    copy->m_ellipseBrush = m_ellipseBrush;
    copy->m_cacheValid = false;
    return copy;
}
