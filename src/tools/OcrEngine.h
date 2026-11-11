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

    // 异步识别。返回 false = 拒绝（已有任务在飞/图为空），**不发信号**——
    // 否则同步发 failed 会误触发上一个在飞请求的回调，把它的结果通道
    // 断掉（表现为"连点几次后识别结果再也不弹"）。调用方自行提示。
    bool recognize(const QImage &image);

    // worker 回调里用来判断结果是否已过期
    quint64 currentRequestId() const { return m_requestId; }

    // 有识别任务在飞时置忙：再点识别直接拒绝（可见反馈），
    // 不再把任务无限堆进线程池，也杜绝"结果过期被丢弃 →
    // 按钮永远停在识别中"的死状态
    bool isBusy() const { return m_busy; }

    // worker 线程投递结果回主线程后调用（清 busy + 按需发信号）
    void deliverResult(quint64 requestId, const WinOcr::Result &result);

signals:
    void finished(const WinOcr::Result &result);
    void failed(const QString &error);

private:
    QString m_language;
    quint64 m_requestId = 0;
    bool m_busy = false;
};
