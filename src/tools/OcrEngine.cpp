#include "tools/OcrEngine.h"

#include "core/ConfigManager.h"

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

namespace {

// 一次识别一个 worker：跑完就丢，避免 WinRT 对象跨线程
class OcrTask : public QRunnable
{
public:
    OcrTask(OcrEngine *owner, quint64 requestId, QImage image, QString language)
        : m_owner(owner)
        , m_requestId(requestId)
        , m_image(std::move(image))
        , m_language(std::move(language))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        const WinOcr::Result result = WinOcr::recognize(m_image, m_language);

        QPointer<OcrEngine> guard(m_owner);
        QMetaObject::invokeMethod(m_owner,
                                  [guard, requestId = m_requestId, result]() {
                                      if (!guard || guard->currentRequestId() != requestId)
                                          return;   // owner 已销毁，或结果已过期
                                      if (result.ok)
                                          emit guard->finished(result);
                                      else
                                          emit guard->failed(result.error);
                                  },
                                  Qt::QueuedConnection);
    }

private:
    OcrEngine *m_owner;
    quint64 m_requestId;
    QImage m_image;
    QString m_language;
};

} // namespace

void OcrEngine::recognize(const QImage &image)
{
    if (image.isNull()) {
        emit failed(QStringLiteral("选区为空"));
        return;
    }

    const quint64 id = ++m_requestId;
    QThreadPool::globalInstance()->start(new OcrTask(this, id, image, m_language));
}
