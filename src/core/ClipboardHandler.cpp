#include "core/ClipboardHandler.h"
#include "core/ConfigManager.h"

#include "tools/TextToImage.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QUrl>

namespace {

// 生成"文件图标 + 文件名"的图像
QImage filePlaceholder(const QFileInfo &fi)
{
    const int W = 320, H = 90;
    constexpr qreal kDpr = 2.0;

    QImage img(W * int(kDpr), H * int(kDpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(kDpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 背景卡片
    p.setPen(QPen(QColor(0xD0, 0xD0, 0xD0), 1));
    p.setBrush(QColor(0xFA, 0xFA, 0xFA));
    p.drawRoundedRect(QRectF(0.5, 0.5, W - 1, H - 1), 8, 8);

    // 文件图标（简化的折角纸张）
    p.setPen(QPen(QColor(0x5A, 0x8A, 0xD0), 2));
    p.setBrush(QColor(0xE8, 0xF0, 0xFA));
    const QRectF page(18, 20, 40, 50);
    p.drawRoundedRect(page, 3, 3);
    p.drawLine(QPointF(46, 20), QPointF(58, 32));
    p.drawLine(QPointF(58, 32), QPointF(58, 70));
    p.drawLine(QPointF(58, 70), QPointF(46, 70));

    // 文件名
    QFont f = p.font();
    f.setPixelSize(15);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x20, 0x20, 0x20));
    QString name = fi.fileName();
    if (name.length() > 26)
        name = name.left(23) + QStringLiteral("...");
    p.drawText(QRect(72, 18, W - 84, 26), Qt::AlignLeft | Qt::AlignVCenter, name);

    // 路径与大小
    f.setPixelSize(11);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(0x80, 0x80, 0x80));
    QString info = fi.absolutePath();
    if (info.length() > 34)
        info = QStringLiteral("...") + info.right(31);

    if (fi.isFile()) {
        const qint64 sz = fi.size();
        QString sizeText;
        if (sz < 1024)
            sizeText = QStringLiteral("%1 B").arg(sz);
        else if (sz < 1024 * 1024)
            sizeText = QStringLiteral("%1 KB").arg(sz / 1024.0, 0, 'f', 1);
        else
            sizeText = QStringLiteral("%1 MB").arg(sz / (1024.0 * 1024.0), 0, 'f', 1);
        info += QStringLiteral("  ·  ") + sizeText;
    }
    p.drawText(QRect(72, 46, W - 84, 20), Qt::AlignLeft | Qt::AlignVCenter, info);

    p.end();
    return img;
}

} // namespace

namespace ClipboardHandler {

bool parseColor(const QString &text, QColor *out)
{
    if (!out)
        return false;

    const QString t = text.trimmed();
    if (t.isEmpty() || t.length() > 48)
        return false;

    // --- HEX 形式 ---
    static const QRegularExpression hexRe(
        QStringLiteral("^#?([0-9a-fA-F]{6}|[0-9a-fA-F]{8}|[0-9a-fA-F]{3})$"));
    const auto hexMatch = hexRe.match(t);
    if (hexMatch.hasMatch()) {
        QString hex = hexMatch.captured(1);
        if (hex.length() == 3) {
            // #abc → #aabbcc
            hex = QStringLiteral("%1%1%2%2%3%3")
                      .arg(hex.at(0), hex.at(1), hex.at(2));
        }
        const QColor c(QStringLiteral("#") + hex);
        if (c.isValid()) {
            *out = c;
            return true;
        }
    }

    // --- RGB 形式：255,128,0 / 255 128 0 / 255 128 0 128 ---
    static const QRegularExpression rgbRe(
        QStringLiteral("^(?:rgb\\s*\\(\\s*)?(\\d{1,3})\\s*[,\\s]\\s*(\\d{1,3})\\s*[,\\s]\\s*(\\d{1,3})"
                       "(?:\\s*[,\\s]\\s*(\\d{1,3}))?\\s*\\)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto rgbMatch = rgbRe.match(t);
    if (rgbMatch.hasMatch()) {
        const int r = rgbMatch.captured(1).toInt();
        const int g = rgbMatch.captured(2).toInt();
        const int b = rgbMatch.captured(3).toInt();
        const int a = rgbMatch.captured(4).isEmpty() ? 255 : rgbMatch.captured(4).toInt();
        if (r <= 255 && g <= 255 && b <= 255 && a <= 255) {
            *out = QColor(r, g, b, a);
            return true;
        }
    }

    return false;
}

Payload fromColorText(const QString &text)
{
    Payload p;
    QColor c;
    if (!parseColor(text, &c))
        return p;

    const int W = 150, H = 96;
    constexpr qreal kDpr = 2.0;

    QImage img(W * int(kDpr), H * int(kDpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(kDpr);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    // 色块
    painter.setPen(QPen(QColor(0, 0, 0, 40), 1));
    painter.setBrush(c);
    painter.drawRoundedRect(QRectF(0.5, 0.5, W - 1, H - 1), 6, 6);

    // 色值文字：按背景亮度选黑/白
    const bool darkBg = (c.lightness() < 128);
    painter.setPen(darkBg ? Qt::white : Qt::black);

    QFont f = painter.font();
    f.setPixelSize(13);
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(QRect(0, H / 2 - 22, W, 20), Qt::AlignCenter,
                     c.name(QColor::HexRgb).toUpper());

    f.setPixelSize(11);
    f.setBold(false);
    painter.setFont(f);
    painter.drawText(QRect(0, H / 2 + 2, W, 18), Qt::AlignCenter,
                     QStringLiteral("RGB(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue()));
    painter.end();

    p.image = img;
    p.sourceText = text.trimmed();
    p.description = QStringLiteral("颜色");
    p.valid = true;
    return p;
}

Payload fromText(const QString &text)
{
    Payload p;
    if (text.trimmed().isEmpty())
        return p;

    TextToImage::Options opt;
    opt.maxWidth = 700;

    const QImage img = TextToImage::renderPlain(text, opt);
    if (img.isNull())
        return p;

    p.image = img;
    p.sourceText = text;
    p.description = QStringLiteral("文本");
    p.valid = true;
    return p;
}

Payload fromHtml(const QString &html)
{
    Payload p;
    if (html.trimmed().isEmpty())
        return p;

    TextToImage::Options opt;
    opt.maxWidth = 700;

    const QImage img = TextToImage::renderHtml(html, opt);
    if (img.isNull())
        return p;

    p.sourceText = html;
    p.image = img;
    p.description = QStringLiteral("HTML");
    p.valid = true;
    return p;
}

QStringList webImageUrls(const QMimeData *mime)
{
    QStringList out;
    if (!mime || !mime->hasUrls())
        return out;

    static const QStringList kImageExt = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("ico"), QStringLiteral("tif"), QStringLiteral("tiff")
    };

    for (const QUrl &url : mime->urls()) {
        const QString scheme = url.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
            continue;
        const QString path = url.path().toLower();
        const QString suffix = path.section(QLatin1Char('.'), -1);
        if (kImageExt.contains(suffix))
            out.append(url.toString());
    }
    return out;
}

QVector<Payload> fromClipboard()
{
    QVector<Payload> out;

    const QClipboard *clip = QGuiApplication::clipboard();
    if (!clip)
        return out;

    const QMimeData *mime = clip->mimeData();
    if (!mime)
        return out;

    // 1) 图像
    if (mime->hasImage()) {
        const QImage img = clip->image();
        if (!img.isNull()) {
            Payload p;
            p.image = img;
            p.description = QStringLiteral("图像");
            p.valid = true;
            out.append(p);
            return out;
        }
    }

    // 2) / 3) 文件路径
    if (mime->hasUrls()) {
        // never_paste_file_path: 文件路径只转图片，不粘贴路径文本
        bool anyFile = false;
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;

            const QFileInfo fi(url.toLocalFile());
            if (!fi.exists())
                continue;

            anyFile = true;

            // 图片文件 → 直接加载
            const QString suffix = fi.suffix().toLower();
            static const QStringList kImageExt = {
                QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
                QStringLiteral("ico"), QStringLiteral("tif"), QStringLiteral("tiff")
            };

            if (kImageExt.contains(suffix)) {
                QImageReader reader(fi.absoluteFilePath());
                reader.setAutoTransform(true);
                const QImage img = reader.read();
                if (!img.isNull()) {
                    Payload p;
                    p.image = img;
                    p.description = QStringLiteral("图片文件");
                    p.valid = true;
                    out.append(p);
                    continue;
                }
            }

            // 非图片 → 生成文件卡片
            Payload p;
            p.image = filePlaceholder(fi);
            p.description = QStringLiteral("文件");
            p.valid = true;
            out.append(p);
        }
        if (anyFile)
            return out;
    }

    // 4) 颜色
    if (mime->hasText()) {
        const QString text = mime->text();
        Payload colorPayload = fromColorText(text);
        if (colorPayload.valid) {
            out.append(colorPayload);
            return out;
        }
    }

    // 5) HTML
    if (mime->hasHtml()) {
        Payload p = fromHtml(mime->html());
        if (p.valid) {
            out.append(p);
            return out;
        }
    }

    // 6) 纯文本
    if (mime->hasText()) {
        Payload p = fromText(mime->text());
        if (p.valid) {
            out.append(p);
            return out;
        }
    }

    return out;
}

} // namespace ClipboardHandler
