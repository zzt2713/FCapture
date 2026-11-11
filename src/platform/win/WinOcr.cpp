#include "platform/win/WinOcr.h"

#include <QDebug>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <windows.foundation.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// IMemoryBufferByteAccess 在 Windows SDK 的 winrt/MemoryBuffer.h 里，
// 但该目录不在默认 include 路径上，这里按 SDK 原样声明一份。
namespace Windows::Foundation {
struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) IMemoryBufferByteAccess
    : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE **value, UINT32 *capacity) = 0;
};
} // namespace Windows::Foundation

using Windows::Foundation::IMemoryBufferByteAccess;

#include <algorithm>
#include <cstring>

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

namespace {

// QImage → SoftwareBitmap（Gray8 灰度）。
//
// Windows.Media.Ocr 原生接受 Gray8 输入（识别前本来就要转灰度），
// 直接喂灰度：
//   1) 位图内存 4 字节/像素 → 1 字节/像素（约省一半）；
//   2) 省掉引擎内部一次 BGRA→灰度转换。
SoftwareBitmap toSoftwareBitmap(const QImage &image)
{
    QImage src = image.convertToFormat(QImage::Format_Grayscale8);
    if (src.isNull())
        return nullptr;

    auto bitmap = SoftwareBitmap(BitmapPixelFormat::Gray8, src.width(), src.height(),
                                 BitmapAlphaMode::Ignore);

    auto buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
    auto reference = buffer.CreateReference();

    ComPtr<IMemoryBufferByteAccess> access;
    if (FAILED(reference.as(IID_PPV_ARGS(access.GetAddressOf()))))
        return nullptr;

    BYTE *data = nullptr;
    UINT32 capacity = 0;
    if (FAILED(access->GetBuffer(&data, &capacity)) || !data)
        return nullptr;

    // QImage 每行可能有 4 字节对齐填充，SoftwareBitmap 的 stride 也由它自己决定，
    // 稳妥起见按行拷（Gray8 每像素 1 字节）。
    const int rowBytes = src.width();
    const int stride = int(capacity / qMax(1u, UINT32(src.height())));
    if (stride <= 0 || stride < rowBytes)
        return nullptr;

    for (int y = 0; y < src.height(); ++y) {
        std::memcpy(data + qsizetype(y) * stride,
                    src.constScanLine(y),
                    size_t(rowBytes));
    }
    return bitmap;
}

} // namespace

namespace {

// Windows OCR 引擎没有"符号"模型：箭头、方框、数学符号会被强行映射成
// 形状相近的汉字（箭头 → 「兮」「一」「→」等）。这里做一层轻量后处理，
// 只处理"整词就是一个形近字"的明显误判，不做任意替换以免误伤正文。
QString fixSymbolMisreads(const QString &text)
{
    // 形近误判映射：左侧是 OCR 输出，右侧是它几乎一定对应的符号
    static const struct { const char *from; const char *to; } kMap[] = {
        { "兮", "→" },
        { "→", "→" },
        { "←", "←" },
        { "↑", "↑" },
        { "↓", "↓" },
    };

    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        bool replaced = false;
        for (const auto &entry : kMap) {
            if (ch == QString::fromUtf8(entry.from).at(0)) {
                out.append(QString::fromUtf8(entry.to));
                replaced = true;
                break;
            }
        }
        if (!replaced)
            out.append(ch);
    }
    return out;
}


// 对比度增强：灰度 → 自动色阶拉伸 → 轻微锐化。
//
// 截图里的浅灰小字（比如深色主题下的次要说明）原始对比度很低，
// OCR 引擎容易把笔画切错。拉伸色阶能把"看起来几乎一样"的笔画分开。
//
// 全程 Format_Grayscale8（1 字节/像素）：以前在 RGB32 上做，
// 2000px 宽的图单次临时缓冲就是 8MB，改后 2MB。
QImage enhanceForOcr(const QImage &src)
{
    if (src.isNull())
        return src;

    QImage img = src.convertToFormat(QImage::Format_Grayscale8);
    if (img.isNull())
        return src;

    const int w = img.width();
    const int h = img.height();
    if (w < 3 || h < 3)
        return src;

    // 1) 直方图（单通道灰度直接就是像素值）
    QVector<int> histogram(256, 0);
    for (int y = 0; y < h; ++y) {
        const auto *line = img.constScanLine(y);
        for (int x = 0; x < w; ++x)
            ++histogram[line[x]];
    }

    // 2) 找到 1% 与 99% 分位，作为拉伸区间
    const int total = w * h;
    const int lowerTarget = total / 100;
    const int upperTarget = total - total / 100;

    int low = 0;
    int high = 255;
    int acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += histogram.at(v);
        if (acc >= lowerTarget) {
            low = v;
            break;
        }
    }
    acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += histogram.at(v);
        if (acc >= upperTarget) {
            high = v;
            break;
        }
    }
    // 区间过窄说明整张图几乎单色，拉伸没有意义（还会放大噪点）
    if (high - low < 24)
        return src;

    const double scale = 255.0 / double(high - low);
    for (int y = 0; y < h; ++y) {
        auto *line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            line[x] = uchar(qBound(0, qRound((line[x] - low) * scale), 255));
        }
    }

    // 3) 轻微锐化（3x3 拉普拉斯），让笔画边缘更硬
    QImage sharpened = img;
    for (int y = 1; y < h - 1; ++y) {
        const auto *prev = img.constScanLine(y - 1);
        const auto *cur = img.constScanLine(y);
        const auto *next = img.constScanLine(y + 1);
        auto *out = sharpened.scanLine(y);
        for (int x = 1; x < w - 1; ++x) {
            const int center = cur[x];
            const int sum = int(prev[x]) + next[x] + cur[x - 1] + cur[x + 1];
            // center*5 - 邻居和，约等于原图 + 拉普拉斯
            out[x] = uchar(qBound(0, center * 5 - sum, 255));
        }
    }
    return sharpened;
}

// 估算正文字高（像素）：灰度投影 → 二值化行带 → 取文字行高。
//
// Windows OCR 对 <40px 高的文字识别率明显下降。把**文字**归一化到
// ~50px 是最有效的精度手段（远好过按图片长边放大：一行 300×40 的
// 长条截图按长边放大 3 倍后字高还是不到 40px，白放大）。
int estimateTextHeight(const QImage &gray)
{
    const int w = gray.width();
    const int h = gray.height();
    if (w < 16 || h < 16)
        return 0;

    // 行投影：统计每行"笔画像素"个数（与行内背景亮度差超过阈值）。
    // 文字行笔画密集，背景行接近 0 —— 阈值取行像素的 4%。
    QVector<bool> textRow(h, false);
    const int inkThreshold = qMax(2, w / 25);
    for (int y = 0; y < h; ++y) {
        const uchar *line = gray.constScanLine(y);
        // 行内背景亮度近似 = 行中位数（对浅底深字/深底浅字都成立）
        int lo = 255, hi = 0;
        for (int x = 0; x < w; ++x) {
            lo = qMin(lo, int(line[x]));
            hi = qMax(hi, int(line[x]));
        }
        const int mid = (lo + hi) / 2;
        int ink = 0;
        for (int x = 0; x < w; ++x) {
            if (qAbs(int(line[x]) - mid) > 32)
                ++ink;
        }
        textRow[y] = ink >= inkThreshold;
    }

    // 取连续文字行带的高度中位数 = 典型字高
    QVector<int> bandHeights;
    int run = 0;
    for (int y = 0; y < h; ++y) {
        if (textRow[y]) {
            ++run;
        } else if (run > 0) {
            if (run >= 6)
                bandHeights.append(run);
            run = 0;
        }
    }
    if (run >= 6)
        bandHeights.append(run);
    if (bandHeights.isEmpty())
        return 0;

    std::sort(bandHeights.begin(), bandHeights.end());
    return bandHeights.at(bandHeights.size() / 2);
}

// 按估算字高把图放大到适合 OCR 的大小（只放大、不缩小）。
//
// 目标：正文字高 ≈ targetTextPx（50 是 Windows OCR 的最佳识别高度）。
// 顶层封顶：放大后最长边不超过 2400（内存与识别耗时的平衡点）。
//
// dpr 语义：返回图的 dpr = 源 dpr × scale，使 runOnce 里
// "坐标 ÷ dpr" 自动还原为**源图逻辑坐标**，文字框不会错位。
QImage upscaleForOcr(const QImage &src, double targetTextPx = 50.0,
                     double maxScale = 6.0)
{
    if (src.isNull())
        return src;

    const double srcDpr = src.devicePixelRatio() > 0.0 ? src.devicePixelRatio() : 1.0;
    const QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    const int textH = estimateTextHeight(gray);

    double scale = 1.0;
    if (textH > 0)
        scale = qBound(1.0, targetTextPx / textH, maxScale);
    // 估不出字高（纯图片/表格线）时退化为长边启发：放大到最长边 1500
    if (textH <= 0) {
        const int longSide = qMax(src.width(), src.height());
        if (longSide > 0 && longSide < 1500)
            scale = qMin(maxScale, 1500.0 / longSide);
    }
    // 总量封顶
    const int longSide = qMax(src.width(), src.height());
    if (longSide * scale > 2400)
        scale = 2400.0 / longSide;

    if (scale <= 1.05)
        return src;

    QImage out = src.scaled(qRound(src.width() * scale),
                            qRound(src.height() * scale),
                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(srcDpr * scale);
    return out;
}

// 结果"可疑"的判定：没有任何词、或平均词长过短（大量单字往往意味着切错）。
bool looksUncertain(const QString &text, int wordCount)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return true;
    if (wordCount == 0)
        return true;

    // 平均每个"词"只有 1 个字符，且总长很短 → 大概率切错了
    const double avg = double(trimmed.size()) / qMax(1, wordCount);
    return trimmed.size() < 12 && avg < 1.6;
}

} // namespace

namespace WinOcr {

QStringList availableLanguages()
{
    QStringList out;
    try {
        for (const auto &lang : OcrEngine::AvailableRecognizerLanguages()) {
            const QString tag = QString::fromWCharArray(lang.LanguageTag().c_str());
            if (!tag.isEmpty())
                out.append(tag);
        }
    } catch (const hresult_error &e) {
        qWarning() << "WinOcr: AvailableRecognizerLanguages failed:" << e.message().c_str();
    }
    return out;
}

QString languageDisplayName(const QString &languageTag)
{
    if (languageTag.isEmpty())
        return {};

    try {
        const winrt::Windows::Globalization::Language lang(languageTag.toStdWString());
        const QString native = QString::fromWCharArray(lang.NativeName().c_str());
        if (!native.isEmpty())
            return QStringLiteral("%1 (%2)").arg(native, languageTag);
    } catch (const hresult_error &) {
        // 无效标签，走回退
    }
    return languageTag;
}

namespace {

// 对一张已准备好的图跑一次 OCR，返回文本 + 词数 + 词表 + 行表。
struct PassResult
{
    bool ok = false;
    QString text;
    QVector<WinOcr::Word> words;
    QVector<WinOcr::Line> lines;
    QString language;
};

// 一次识别结果的可信度评分：文本越长、词数越多越可信。
// 用作多个候选之间的择优依据。
int scoreOf(const PassResult &pass)
{
    if (!pass.ok || pass.words.isEmpty())
        return 0;
    return pass.text.trimmed().size() * 100 + pass.words.size();
}

// 语言引擎选错的判定：Windows OCR 是单语言引擎，语言不匹配时输出的是
// "形近乱码"而非报错。判据 = 输出的字符集与所用语言的书写系统不符：
//   - 中文引擎却几乎全是拉丁字母 → 应该用英文引擎
//   - 拉丁引擎却出现大量汉字     → 应该用中文引擎
bool looksWrongLanguage(const QString &text, const QString &language)
{
    int latin = 0, cjk = 0, total = 0;
    for (const QChar ch : text) {
        if (ch.isSpace() || ch.isDigit() || ch.isPunct())
            continue;
        ++total;
        const uint u = ch.unicode();
        if (u >= 0x4E00 && u <= 0x9FFF)
            ++cjk;
        else if (ch.isLetter())
            ++latin;
    }
    if (total < 12)
        return false;   // 太短的文本不判（误差大）

    const bool zhEngine = language.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
    if (zhEngine)
        return cjk * 2 < latin;      // 中文引擎输出里拉丁字母占多数
    return cjk > 0 && cjk * 1 >= latin;  // 拉丁引擎输出里汉字不少于拉丁
}

// 选一个"与当前语言互补"的备用语言：中文 → 英文包，英文/其它 → 中文包。
// 都没有就返回空（不纠错）。
QString pickAlternateLanguage(const QString &current)
{
    const QStringList all = WinOcr::availableLanguages();
    const bool zhNow = current.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
    for (const QString &tag : all) {
        const bool zh = tag.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);
        if (zh != zhNow)
            return tag;
    }
    return {};
}

PassResult runOnce(winrt::Windows::Media::Ocr::OcrEngine engine, const QImage &image)
{
    PassResult pass;
    if (!engine || image.isNull())
        return pass;

    auto bitmap = toSoftwareBitmap(image);
    if (!bitmap)
        return pass;

    const winrt::Windows::Media::Ocr::OcrResult ocr = engine.RecognizeAsync(bitmap).get();

    const double dpr = image.devicePixelRatio() > 0.0 ? image.devicePixelRatio() : 1.0;
    QStringList lineTexts;
    for (const auto &line : ocr.Lines()) {
        WinOcr::Line info;
        for (const auto &word : line.Words()) {
            const QString w = fixSymbolMisreads(
                QString::fromWCharArray(word.Text().c_str()));
            if (!w.isEmpty()) {
                if (!info.text.isEmpty())
                    info.text += QLatin1Char(' ');
                info.text += w;
            }

            const auto r = word.BoundingRect();
            WinOcr::Word wordInfo;
            wordInfo.text = w;
            wordInfo.box = QRectF(r.X / dpr, r.Y / dpr, r.Width / dpr, r.Height / dpr);
            pass.words.append(wordInfo);
            info.words.append(wordInfo);
            info.box = info.box.united(wordInfo.box);
        }
        if (!info.text.isEmpty()) {
            pass.lines.append(info);
            lineTexts.append(info.text);
        }
    }

    pass.text = lineTexts.join(QLatin1Char('\n'));
    pass.ok = true;
    // 记录实际用的语言（换语言纠错时结果要跟着标注语言）
    pass.language = QString::fromWCharArray(
        engine.RecognizerLanguage().LanguageTag().c_str());
    return pass;
}

} // namespace

Result recognize(const QImage &image, const QString &language, const Options &options)
{
    Result result;
    if (image.isNull()) {
        result.error = QStringLiteral("图像为空");
        return result;
    }

    try {
        // 工作线程里必须自己初始化 WinRT 公寓（MTA）
        static thread_local bool apartmentInitialized = false;
        if (!apartmentInitialized) {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized = true;
        }

        OcrEngine engine = nullptr;
        if (!language.isEmpty()) {
            try {
                engine = OcrEngine::TryCreateFromLanguage(
                    winrt::Windows::Globalization::Language(language.toStdWString()));
            } catch (const hresult_error &) {
                engine = nullptr;
            }
        }
        if (!engine)
            engine = OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine) {
            result.error = QStringLiteral("没有可用的 OCR 语言包");
            return result;
        }

        // 按**文字高度**归一化放大（目标字高 50px，Windows OCR 最佳识别高度）。
        // 不按图片长边放大：一行 300×40 的截图按长边放 3 倍后字高仍然不到
        // 40px，白放大 —— 小字识别的瓶颈是字高，不是图片尺寸。
        QImage base = upscaleForOcr(image, 50.0, 6.0);

        // 主路径：原图（已放大）
        PassResult best = runOnce(engine, base);
        int bestScore = scoreOf(best);

        // 快速通道：首道结果就足够好时直接收工（常见清晰截图）。
        // 一次识别 1 道 vs 2~4 道，耗时砍半以上 —— 快速连点时任务不再
        // 排长队（"点得快就没反应，过一会又好"的根源）。
        const bool firstPassGood = best.ok && !best.text.trimmed().isEmpty()
                                   && best.text.trimmed().size() >= 12
                                   && !looksUncertain(best.text, best.words.size())
                                   && !looksWrongLanguage(best.text, best.language);
        if (firstPassGood) {
            result.text = best.text;
            result.words = best.words;
            result.lines = best.lines;
            result.ok = true;
            result.language = best.language;
            return result;
        }

        // 首道结果偏弱：增强图并行比较 —— 浅色/低对比截图里增强明显更好。
        if (options.enhanceContrast) {
            QImage enhanced = enhanceForOcr(base);
            enhanced.setDevicePixelRatio(base.devicePixelRatio());
            const PassResult alt = runOnce(engine, enhanced);
            const int altScore = scoreOf(alt);
            if (altScore > bestScore) {
                best = alt;
                bestScore = altScore;
            }
        }

        if (!best.ok || best.text.trimmed().isEmpty()) {
            result.error = QStringLiteral("未识别到文字");
            return result;
        }

        // 语言引擎纠错：Windows OCR 是单语言引擎，语言选错时输出整段
        // 形近乱码（中文图配英文引擎 → 碎片拉丁字母，反之亦然），
        // 用户感受就是"识别不准确"。按脚本特征判定语言不匹配时，
        // 换另一种主流语言再试一次，取更好的结果。
        if (options.retryWhenUncertain
            && looksWrongLanguage(best.text, best.language)) {
            const QString altTag = pickAlternateLanguage(best.language);
            if (!altTag.isEmpty()) {
                try {
                    const OcrEngine altEngine = OcrEngine::TryCreateFromLanguage(
                        winrt::Windows::Globalization::Language(altTag.toStdWString()));
                    if (altEngine) {
                        const PassResult alt = runOnce(altEngine, base);
                        const int altScore = scoreOf(alt);
                        if (altScore > bestScore) {
                            best = alt;
                            bestScore = altScore;
                        }
                    }
                } catch (const hresult_error &) {
                    // 备用语言创建失败：保留当前结果
                }
            }
        }

        // 二次识别：结果"可疑"时以更高目标字高再放大重跑。
        // 只补一次，不做多档连试。
        if (options.retryWhenUncertain
            && looksUncertain(best.text, best.words.size())) {
            const QImage retryBase = upscaleForOcr(image, 80.0, 10.0);
            if (retryBase.size() != base.size()) {
                const PassResult pass = runOnce(engine, retryBase);
                if (scoreOf(pass) > bestScore) {
                    best = pass;
                    bestScore = scoreOf(pass);
                }
            }
        }

        result.text = best.text;
        result.words = best.words;
        result.lines = best.lines;
        result.ok = true;
        result.language = best.language.isEmpty()
                              ? QString::fromWCharArray(
                                    engine.RecognizerLanguage().LanguageTag().c_str())
                              : best.language;
    } catch (const hresult_error &e) {
        result.error = QString::fromWCharArray(e.message().c_str());
        qWarning() << "WinOcr: recognize failed:" << result.error;
    } catch (...) {
        result.error = QStringLiteral("OCR 未知错误");
    }

    return result;
}

} // namespace WinOcr
