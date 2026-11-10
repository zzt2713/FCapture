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

// QImage → SoftwareBitmap。
// QImage 的 Format_ARGB32 在小端机器上字节序就是 BGRA，直接按 Bgra8 拷过去。
SoftwareBitmap toSoftwareBitmap(const QImage &image)
{
    QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (src.isNull())
        return nullptr;

    auto bitmap = SoftwareBitmap(BitmapPixelFormat::Bgra8, src.width(), src.height(),
                                 BitmapAlphaMode::Premultiplied);

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
    // 稳妥起见按行拷。
    const int rowBytes = src.width() * 4;
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
QImage enhanceForOcr(const QImage &src)
{
    if (src.isNull())
        return src;

    QImage img = src.convertToFormat(QImage::Format_RGB32);
    if (img.isNull())
        return src;

    // 1) 转灰度并统计直方图（只取亮度）
    const int w = img.width();
    const int h = img.height();
    QVector<int> histogram(256, 0);
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            ++histogram[qGray(line[x])];
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
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = qGray(line[x]);
            const int mapped = qBound(0, qRound((v - low) * scale), 255);
            line[x] = qRgb(mapped, mapped, mapped);
        }
    }

    // 3) 轻微锐化（3x3 拉普拉斯），让笔画边缘更硬
    QImage sharpened = img;
    for (int y = 1; y < h - 1; ++y) {
        const auto *prev = reinterpret_cast<const QRgb *>(img.constScanLine(y - 1));
        const auto *cur = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        const auto *next = reinterpret_cast<const QRgb *>(img.constScanLine(y + 1));
        auto *out = reinterpret_cast<QRgb *>(sharpened.scanLine(y));
        for (int x = 1; x < w - 1; ++x) {
            const int center = qGray(cur[x]);
            const int sum = qGray(prev[x]) + qGray(next[x])
                            + qGray(cur[x - 1]) + qGray(cur[x + 1]);
            // center*5 - 邻居和，约等于原图 + 拉普拉斯
            const int v = qBound(0, center * 5 - sum, 255);
            out[x] = qRgb(v, v, v);
        }
    }
    return sharpened;
}

// 把图像放大到"短边接近 targetShortSide"（只放大、不缩小）
QImage upscaleForOcr(const QImage &src, int targetShortSide, double maxScale)
{
    if (src.isNull())
        return src;

    const double dpr = src.devicePixelRatio() > 0.0 ? src.devicePixelRatio() : 1.0;
    const QSize logical(qRound(src.width() / dpr), qRound(src.height() / dpr));
    const int shortSide = qMin(logical.width(), logical.height());
    if (shortSide <= 0 || shortSide >= targetShortSide)
        return src;

    const double scale = qBound(1.0, double(targetShortSide) / shortSide, maxScale);
    if (scale <= 1.05)
        return src;

    QImage out = src.scaled(qRound(src.width() * scale),
                            qRound(src.height() * scale),
                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
    out.setDevicePixelRatio(dpr * scale);
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

        // 只跑一次主路径（snow 的做法：一次识别，不反复试候选）。
        // 以前默认会跑 2 个候选 + 可疑时再跑 2 个 = 最多 4 次识别，
        // 大图下就是 4 倍耗时 —— 这是"OCR 慢死了"的根因。
        // 增强作为**失败兜底**：主路径没识别到文字时才换增强图再试一次。
        QImage base = upscaleForOcr(image, 800, 4.0);

        PassResult best = runOnce(engine, base);
        if ((!best.ok || best.text.trimmed().isEmpty()) && options.enhanceContrast) {
            QImage enhanced = enhanceForOcr(base);
            enhanced.setDevicePixelRatio(base.devicePixelRatio());
            const PassResult retry = runOnce(engine, enhanced);
            if (retry.ok && !retry.text.trimmed().isEmpty())
                best = retry;
        }

        int bestScore = best.words.isEmpty()
                            ? 0
                            : best.text.trimmed().size() * 100 + best.words.size();

        if (!best.ok || best.text.trimmed().isEmpty()) {
            result.error = QStringLiteral("未识别到文字");
            return result;
        }

        // 二次识别：仅当结果"可疑"（且用户开了该选项）才放大重跑一次。
        // 这是最后的补偿手段，默认不再无条件触发。
        if (options.retryWhenUncertain
            && looksUncertain(best.text, best.words.size())) {
            const QImage retryBase = upscaleForOcr(image, 1400, 6.0);
            if (retryBase.size() != base.size()) {
                for (const QImage &candidate :
                     QVector<QImage>{ retryBase, enhanceForOcr(retryBase) }) {
                    PassResult pass = runOnce(engine, candidate);
                    if (!pass.ok || pass.words.isEmpty())
                        continue;
                    const int score = pass.text.trimmed().size() * 100
                                      + pass.words.size();
                    if (score > bestScore) {
                        bestScore = score;
                        best = pass;
                    }
                }
            }
        }

        result.text = best.text;
        result.words = best.words;
        result.lines = best.lines;
        result.ok = true;
        result.language = QString::fromWCharArray(
            engine.RecognizerLanguage().LanguageTag().c_str());
    } catch (const hresult_error &e) {
        result.error = QString::fromWCharArray(e.message().c_str());
        qWarning() << "WinOcr: recognize failed:" << result.error;
    } catch (...) {
        result.error = QStringLiteral("OCR 未知错误");
    }

    return result;
}

} // namespace WinOcr
