#pragma once

#include <QColor>
#include <QImage>
#include <QString>

// 把文本 / HTML 渲染成图片（用于贴图）
namespace TextToImage {

struct Options
{
    int     maxWidth = 0;                       // 0 = 不限制
    QString fontFamily = QStringLiteral("Microsoft YaHei UI");
    int     fontSize = 13;
    QColor  bgColor = QColor(0xFF, 0xFF, 0xFF);
    QColor  fgColor = QColor(0x20, 0x20, 0x20);
    int     padding = 10;
    int     borderRadius = 0;
};

// 纯文本 → 图片
QImage renderPlain(const QString &text, const Options &opt);

// HTML → 图片
QImage renderHtml(const QString &html, const Options &opt);

} // namespace TextToImage
