#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

// Windows.Media.Ocr 封装。
//
// 这是整个工程里唯一允许出现 winrt 头的实现文件所对应的接口——
// 头文件本身保持干净（不引入 winrt），调用方只看到 QImage / QString。
namespace WinOcr {

struct Word
{
    QString text;
    QRectF  box;      // 逻辑坐标，相对源图左上角
};

struct Result
{
    QString     text;         // 整段文本（保留换行）
    QVector<Word> words;      // 每个词的包围盒
    QString     language;     // 实际使用的 BCP-47 语言
    bool        ok = false;
    QString     error;
};

// 已安装的识别语言（BCP-47，如 "zh-Hans-CN"）
QStringList availableLanguages();

// 语言代码 → 人类可读名称（如 "zh-Hans-CN" → "中文（简体，中国）"）。
// 取不到时回退原代码。
QString languageDisplayName(const QString &languageTag);


// 识别选项
struct Options
{
    // 识别前做对比度增强（灰度 + 自动色阶拉伸 + 锐化）。
    // 对浅色小字、低对比度截图有效；关闭时只做必要的放大。
    bool enhanceContrast = true;

    // 第一次结果"可疑"时，用 1.6 倍放大再识别一次并取更好的那份。
    // 代价约多一倍耗时，只会在可疑时触发。
    bool retryWhenUncertain = true;
};

// 同步识别。**必须在非 UI 线程调用**（RecognizeAsync().get() 会阻塞）。
// language 为空时用系统用户配置语言。
Result recognize(const QImage &image, const QString &language = QString(),
                 const Options &options = Options());

} // namespace WinOcr
