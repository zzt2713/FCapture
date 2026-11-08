#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

class QMimeData;

// 把剪贴板内容解析成可贴的图像
namespace ClipboardHandler {

struct Payload
{
    QImage  image;
    QString sourceText;      // 若由文本/颜色转换而来，保存原文
    QString description;     // 内容类型描述（用于日志/提示）
    bool    valid = false;
};

// 解析当前剪贴板，返回一个或多个待贴图像
QVector<Payload> fromClipboard();

// 剪贴板里的网页图片 URL（http/https + 图片扩展名）。
// 这类内容需要异步下载，不能放在 fromClipboard 里同步处理。
QStringList webImageUrls(const QMimeData *mime);

// 单个字符串 → 图像（供内部和调试用）
Payload fromText(const QString &text);
Payload fromHtml(const QString &html);
Payload fromColorText(const QString &text);

// 判断字符串是否是颜色值，解析成功返回 true
bool parseColor(const QString &text, QColor *out);

} // namespace ClipboardHandler
