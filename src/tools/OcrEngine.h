#pragma once

#include "platform/win/WinOcr.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>

// OCR 引擎的 Qt 门面：把阻塞的 Windows.Media.Ocr 调用放到线程池里跑。
//
// 线程模型：
//   recognize() 在 UI 线程调用 → QThreadPool 里跑 WinOcr::recognize()
//   → 结果用 QueuedConnection 回到 UI 线程发 finished/failed
// C++/WinRT 有线程亲和性，所以每次识别都新建一个 worker，不复用 OcrEngine 对象。
class OcrEngine : public QObject
{
    Q_OBJECT

public:
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;

    // 已安装语言（同步，很快）
    static QStringList availableLanguages();

    void setLanguage(const QString &bcp47) { m_language = bcp47; }
    QString language() const { return m_language; }

    // 异步识别；重复调用会让上一个请求的结果作废
    void recognize(const QImage &image);

    // worker 回调里用来判断结果是否已过期
    quint64 currentRequestId() const { return m_requestId; }

signals:
    void finished(const WinOcr::Result &result);
    void failed(const QString &error);

private:
    QString m_language;
    quint64 m_requestId = 0;
};
