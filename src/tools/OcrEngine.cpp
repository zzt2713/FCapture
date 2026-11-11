#include "tools/OcrEngine.h"

#include "core/ConfigManager.h"

#include <QCoreApplication>
#include <QMetaType>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>

OcrEngine::OcrEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<WinOcr::Result>("WinOcr::Result");

    m_language = ConfigManager::instance()->value(QStringLiteral("ocr/language")).toString();
}

OcrEngine::~OcrEngine() = default;

QStringList OcrEngine::availableLanguages()
{
    return WinOcr::availableLanguages();
}

void OcrEngine::deliverResult(quint64 requestId, const WinOcr::Result &result)
{
    m_busy = false;
    if (requestId != m_requestId)
        return;   // 结果已过期（期间又发起了新请求）：静默丢弃
    if (result.ok)
        emit finished(result);
    else
        emit failed(result.error);
}

namespace {

// 一次识别一个 worker：跑完就丢，避免 WinRT 对象跨线程。
//
// owner 用 QPointer **在构造时**捕获：识别是阻塞的（几秒），期间
// OcrEngine（对话框/截图层的子对象）随时可能被销毁。以前在这里存裸
// 指针、识别完再拿裸指针构造 QPointer —— 对象已销毁时那就是悬空指针，
// invokeMethod(悬空指针) 是 UB（对应多次 0xC0000005 崩溃），
// 而且结果被丢弃时 finished/failed 都不发，按钮永远停在"识别中"。
class OcrTask : public QRunnable
{
public:
    OcrTask(QPointer<OcrEngine> owner, quint64 requestId, QImage image, QString language)
        : m_owner(owner)
        , m_requestId(requestId)
        , m_image(std::move(image))
        , m_language(std::move(language))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // 开跑前先查过期：快速连点时队列里会积压一堆"注定被丢弃"的任务，
        // 每个还要跑 2~4 次 Windows OCR（几秒）——新任务只能排在后面干等，
        // 表现为"点得快就没反应，过一会又好了"。过期任务直接短路退出。
        if (!m_owner || m_owner->currentRequestId() != m_requestId)
            return;

        // 增强开关来自配置（首选项 → 截图 → 识别增强）
        WinOcr::Options options;
        options.enhanceContrast = ConfigManager::instance()
                                      ->value(QStringLiteral("ocr/enhance"), true)
                                      .toBool();
        options.retryWhenUncertain = options.enhanceContrast;

        WinOcr::Result result = WinOcr::recognize(m_image, m_language, options);
        m_image = QImage();   // 尽早释放大图

        // 结果回主线程投递；QPointer 在主线程解引用是安全的。
        // 用 qApp 作 context：OcrEngine 可能已销毁，回调里先判空。
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [owner = m_owner, requestId = m_requestId, result = std::move(result)]() {
                if (!owner)
                    return;
                owner->deliverResult(requestId, result);
            },
            Qt::QueuedConnection);
    }

private:
    QPointer<OcrEngine> m_owner;
    quint64 m_requestId;
    QImage m_image;
    QString m_language;
};

} // namespace

bool OcrEngine::recognize(const QImage &image)
{
    if (image.isNull())
        return false;

    if (m_busy)
        return false;   // 不发信号：调用方负责提示，避免误触发旧请求的回调

    m_busy = true;
    const quint64 id = ++m_requestId;
    QThreadPool::globalInstance()->start(new OcrTask(QPointer<OcrEngine>(this), id,
                                                     image, m_language));
    return true;
}
