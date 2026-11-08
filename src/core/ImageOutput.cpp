#include "core/ImageOutput.h"

#include "core/ConfigManager.h"

#include <QFileInfo>
#include <QImage>
#include <QImageWriter>

namespace ImageOutput {

int configuredQuality()
{
    const int quality =
        ConfigManager::instance()->value(QStringLiteral("output/quality"), -1).toInt();
    if (quality < 0)
        return -1;
    return qBound(0, quality, 100);
}

bool save(const QImage &image, const QString &path)
{
    if (image.isNull() || path.isEmpty())
        return false;

    const QString suffix = QFileInfo(path).suffix().toLower();
    QByteArray format;
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg"))
        format = QByteArrayLiteral("jpeg");
    else if (suffix == QStringLiteral("webp"))
        format = QByteArrayLiteral("webp");
    else if (!suffix.isEmpty())
        format = suffix.toLatin1();

    QImageWriter writer(path, format);
    if (format == QByteArrayLiteral("jpeg") || format == QByteArrayLiteral("webp")) {
        const int quality = configuredQuality();
        if (quality >= 0)
            writer.setQuality(quality);
    }
    return writer.write(image);
}

} // namespace ImageOutput
