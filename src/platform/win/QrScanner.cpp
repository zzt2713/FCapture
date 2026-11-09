#include "platform/win/QrScanner.h"

#include "ReadBarcode.h"

namespace QrScanner {

QVector<Result> scan(const QImage &image, bool tryHarder)
{
    QVector<Result> out;
    if (image.isNull())
        return out;

    // ZXing 直接吃灰度图最快；彩色图先转灰度可省掉内部转换
    const QImage gray = image.format() == QImage::Format_Grayscale8
                            ? image
                            : image.convertToFormat(QImage::Format_Grayscale8);
    if (gray.isNull())
        return out;

    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::Any);
    options.setTryHarder(tryHarder);
    options.setTryRotate(true);
    options.setTryDownscale(true);

    // QImage 每行有 4 字节对齐（bytesPerLine >= width），
    // 不传 rowStride 会让 ZXing 按 width 逐行读，整张图错位 → 永远识别不出
    const ZXing::ImageView view(gray.constBits(), gray.width(), gray.height(),
                                ZXing::ImageFormat::Lum, gray.bytesPerLine());

    const auto results = ZXing::ReadBarcodes(view, options);
    for (const auto &r : results) {
        if (!r.isValid())
            continue;

        Result item;
        item.text = QString::fromStdString(r.text());
        item.format = QString::fromStdString(ZXing::ToString(r.format()));

        const ZXing::Position pos = r.position();
        if (pos.size() >= 2) {
            qreal minX = pos[0].x;
            qreal maxX = pos[0].x;
            qreal minY = pos[0].y;
            qreal maxY = pos[0].y;
            for (const auto &p : pos) {
                minX = qMin(minX, static_cast<qreal>(p.x));
                maxX = qMax(maxX, static_cast<qreal>(p.x));
                minY = qMin(minY, static_cast<qreal>(p.y));
                maxY = qMax(maxY, static_cast<qreal>(p.y));
            }
            item.position = QRectF(minX, minY, maxX - minX, maxY - minY);
        }

        out.append(item);
    }
    return out;
}

bool hasAny(const QImage &image)
{
    return !scan(image, false).isEmpty();
}

} // namespace QrScanner
