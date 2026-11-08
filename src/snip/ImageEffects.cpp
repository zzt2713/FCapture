#include "snip/ImageEffects.h"

#include <QPainter>
#include <QPainterPath>
#include <QVector>

namespace {

// 单方向盒式模糊（只处理 alpha 通道，阴影不需要颜色）
void blurAlphaPass(const QVector<uchar> &src, QVector<uchar> *dst,
                   int w, int h, int radius, bool horizontal)
{
    const int r = qMax(1, radius);
    const int span = r * 2 + 1;
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;

    for (int o = 0; o < outer; ++o) {
        auto at = [&](int idx) -> int {
            const int c = qBound(0, idx, inner - 1);
            return horizontal ? src.at(o * w + c) : src.at(c * w + o);
        };

        int sum = 0;
        for (int k = -r; k <= r; ++k)
            sum += at(k);

        for (int i = 0; i < inner; ++i) {
            const uchar v = uchar(sum / span);
            if (horizontal)
                (*dst)[o * w + i] = v;
            else
                (*dst)[i * w + o] = v;

            sum += at(i + r + 1) - at(i - r);
        }
    }
}

// 三遍盒式模糊近似高斯
QVector<uchar> blurAlpha(const QVector<uchar> &src, int w, int h, int radius)
{
    QVector<uchar> a = src;
    QVector<uchar> b(src.size(), 0);

    for (int pass = 0; pass < 3; ++pass) {
        blurAlphaPass(a, &b, w, h, radius, true);
        blurAlphaPass(b, &a, w, h, radius, false);
    }
    return a;
}

} // namespace

namespace ImageEffects {

int clampCornerRadius(int radius, const QSize &size)
{
    if (radius <= 0 || size.isEmpty())
        return 0;
    return qMin(radius, qMin(size.width(), size.height()) / 2);
}

QImage applyRoundedCorners(const QImage &src, int radius)
{
    if (src.isNull() || radius <= 0)
        return src;

    const int r = clampCornerRadius(radius, src.size());
    if (r <= 0)
        return src;

    QImage result(src.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(result.rect()), r, r);
    p.setClipPath(path);
    p.drawImage(0, 0, src);
    p.end();

    return result;
}

QImage applyShadow(const QImage &src, int blur, int offset, int border)
{
    if (src.isNull())
        return src;

    blur = qMax(0, blur);
    offset = qMax(0, offset);
    border = qMax(0, border);
    if (blur == 0 && border == 0)
        return src;

    const int padX = blur + border;
    const int padTop = blur + border;
    const int padBottom = blur + offset + border;

    QImage result(src.width() + padX * 2, src.height() + padTop + padBottom,
                  QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    // ---- 阴影层：图片形状的 alpha 蒙版 → 模糊 ----
    if (blur > 0) {
        const int sw = src.width() + border * 2;
        const int sh = src.height() + border * 2;

        QVector<uchar> mask(sw * sh, 0);
        // 圆角矩形蒙版（边框与图片同形）
        QImage shape(sw, sh, QImage::Format_ARGB32_Premultiplied);
        shape.fill(Qt::transparent);
        {
            QPainter sp(&shape);
            sp.setRenderHint(QPainter::Antialiasing);
            sp.setPen(Qt::NoPen);
            sp.setBrush(Qt::black);
            sp.drawRect(QRectF(border, border, src.width(), src.height()));
            sp.end();
        }
        for (int y = 0; y < sh; ++y) {
            const auto *line = reinterpret_cast<const QRgb *>(shape.constScanLine(y));
            for (int x = 0; x < sw; ++x)
                mask[y * sw + x] = uchar(qAlpha(line[x]));
        }

        const QVector<uchar> blurred = blurAlpha(mask, sw, sh, blur);

        QImage shadowLayer(sw, sh, QImage::Format_ARGB32_Premultiplied);
        shadowLayer.fill(Qt::transparent);
        for (int y = 0; y < sh; ++y) {
            auto *line = reinterpret_cast<QRgb *>(shadowLayer.scanLine(y));
            for (int x = 0; x < sw; ++x) {
                const int a = int(blurred.at(y * sw + x)) * 0.45;   // 阴影不透明度
                line[x] = qRgba(0, 0, 0, a);
            }
        }

        // 阴影层本身含 border 的余量，所以贴在 (blur, blur + offset)：
        // 它比原图大 border，再向下偏移 offset
        QPainter p(&result);
        p.drawImage(blur, blur + offset, shadowLayer);
        p.end();
    }

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);

    // ---- 描边 ----
    if (border > 0) {
        p.setPen(QPen(QColor(0xD8, 0xD8, 0xD8), border));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(padX - border / 2.0, padTop - border / 2.0,
                          src.width() + border, src.height() + border));
    }

    // ---- 原图 ----
    p.drawImage(padX, padTop, src);
    p.end();

    return result;
}

QImage flattenOnWhite(const QImage &src)
{
    if (src.isNull())
        return src;

    QImage result(src.size(), QImage::Format_RGB32);
    result.fill(Qt::white);

    QPainter p(&result);
    p.drawImage(0, 0, src);
    p.end();
    return result;
}

} // namespace ImageEffects
