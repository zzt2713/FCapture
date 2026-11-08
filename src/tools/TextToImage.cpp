#include "tools/TextToImage.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QTextOption>

namespace {

QImage renderDocument(QTextDocument &doc, const TextToImage::Options &opt)
{
    QFont f(opt.fontFamily);
    f.setPixelSize(opt.fontSize);
    doc.setDefaultFont(f);
    doc.setDocumentMargin(0);

    if (opt.maxWidth > 0) {
        doc.setTextWidth(opt.maxWidth);
        QTextOption o = doc.defaultTextOption();
        o.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc.setDefaultTextOption(o);
    } else {
        doc.setTextWidth(-1);
    }

    // 强制完成布局
    doc.size();

    QSizeF contentSize = doc.size();
    if (opt.maxWidth <= 0)
        contentSize.setWidth(doc.idealWidth());

    const int pad = opt.padding;
    const QSize imgSize(qCeil(contentSize.width()) + pad * 2,
                        qCeil(contentSize.height()) + pad * 2);

    // 2x 渲染避免高分屏模糊
    constexpr qreal kDpr = 2.0;
    QImage img(imgSize * int(kDpr), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(kDpr);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    if (opt.bgColor.alpha() > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(opt.bgColor);
        if (opt.borderRadius > 0)
            p.drawRoundedRect(QRectF(0, 0, imgSize.width(), imgSize.height()),
                              opt.borderRadius, opt.borderRadius);
        else
            p.drawRect(QRectF(0, 0, imgSize.width(), imgSize.height()));
    }

    p.translate(pad, pad);
    doc.drawContents(&p, QRectF(0, 0, contentSize.width() + 1, contentSize.height() + 1));
    p.end();

    return img;
}

} // namespace

namespace TextToImage {

QImage renderPlain(const QString &text, const Options &opt)
{
    if (text.isEmpty())
        return {};

    QTextDocument doc;

    // QTextDocument 没有 setTextColor，用默认样式表设颜色
    doc.setDefaultStyleSheet(
        QStringLiteral("body { color: %1; }").arg(opt.fgColor.name(QColor::HexRgb)));
    doc.setPlainText(text);

    QImage img = renderDocument(doc, opt);
    return img;
}

QImage renderHtml(const QString &html, const Options &opt)
{
    if (html.isEmpty())
        return {};

    QTextDocument doc;
    doc.setDefaultStyleSheet(
        QStringLiteral("body { color: %1; }").arg(opt.fgColor.name(QColor::HexRgb)));
    doc.setHtml(html);

    return renderDocument(doc, opt);
}

} // namespace TextToImage
