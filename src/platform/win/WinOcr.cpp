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

Result recognize(const QImage &image, const QString &language)
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

        // Windows OCR 对小于约 40px 高的文字识别率明显下降。
        // 官方建议先把图像放大到 2~3 倍再识别，这也是"识别不准"的主因之一。
        // 只放大小图，大图保持原样（避免不必要的耗时）。
        QImage prepared = image;
        const double dpr = prepared.devicePixelRatio() > 0.0
                               ? prepared.devicePixelRatio() : 1.0;
        const QSize logical(qRound(prepared.width() / dpr),
                            qRound(prepared.height() / dpr));
        const int minSide = qMin(logical.width(), logical.height());
        if (minSide > 0 && minSide < 400) {
            // 目标：短边约 800px（约 2~3 倍），上限 4 倍避免过度放大产生噪点
            const double scale = qBound(2.0, 800.0 / minSide, 4.0);
            prepared = prepared.scaled(qRound(prepared.width() * scale),
                                       qRound(prepared.height() * scale),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            prepared.setDevicePixelRatio(dpr * scale);
        }

        auto bitmap = toSoftwareBitmap(prepared);
        if (!bitmap) {
            result.error = QStringLiteral("图像格式转换失败");
            return result;
        }

        // RecognizeAsync().get() 会阻塞，所以本函数只能在非 UI 线程调用
        const OcrResult ocr = engine.RecognizeAsync(bitmap).get();

        QStringList lines;
        for (const auto &line : ocr.Lines()) {
            QString lineText;
            for (const auto &word : line.Words()) {
                const QString w = fixSymbolMisreads(
                    QString::fromWCharArray(word.Text().c_str()));
                if (!w.isEmpty()) {
                    if (!lineText.isEmpty())
                        lineText += QLatin1Char(' ');
                    lineText += w;
                }

                const auto r = word.BoundingRect();
                Word info;
                info.text = w;
                // WinRT 返回的是像素坐标，换算成逻辑坐标
                info.box = QRectF(r.X / dpr, r.Y / dpr, r.Width / dpr, r.Height / dpr);
                result.words.append(info);
            }
            if (!lineText.isEmpty())
                lines.append(lineText);
        }

        result.text = lines.join(QLatin1Char('\n'));
        result.language = QString::fromWCharArray(engine.RecognizerLanguage().LanguageTag().c_str());
        result.ok = true;
    } catch (const hresult_error &e) {
        result.error = QString::fromWCharArray(e.message().c_str());
        qWarning() << "WinOcr: recognize failed:" << result.error;
    } catch (...) {
        result.error = QStringLiteral("OCR 未知错误");
    }

    return result;
}

} // namespace WinOcr
