#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>

#include <functional>
#include <memory>

// 实时窗口捕获（Windows Graphics Capture）
//
// 对齐 snow 的 LiveWindowSource：把一个**正在运行的窗口**实时镜像到贴图上，
// 而不是截一张静态图。窗口内容变化（滚动、播放、输入）会跟着更新。
//
// 实现路径：
//   GraphicsCaptureItem::CreateFromWindowId
//     → Direct3D11CaptureFramePool（每帧回调）
//     → IDirect3DSurface → CPU 回读 → QImage
//
// 只在 src/platform/win/ 下引 WinRT 头（项目约定）。
class LiveWindowSource : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Initializing,
        Live,          // 正常出帧
        Minimized,     // 被最小化，最后一帧定格
        Closed,        // 窗口已关闭
        Unavailable,   // 暂时取不到（锁屏、GPU 重置等），会自动重试
    };

    explicit LiveWindowSource(QObject *parent = nullptr);
    ~LiveWindowSource() override;

    // 绑定目标窗口并开始捕获。windowId 用 quintptr 传递，
    // 避免把 HWND 类型暴露到公共头里。
    bool start(quintptr windowId, QString *error = nullptr);
    void stop();

    bool isRunning() const;
    State state() const { return m_state; }
    QString stateMessage() const { return m_stateMessage; }
    QRect windowRect() const { return m_windowRect; }
    QSize frameSize() const { return m_lastFrame.size(); }

    // 最后一次成功读到的帧（首帧同步取用；后续靠 frameReady）
    QImage currentFrame() const { return m_lastFrame; }

signals:
    // 每来一帧发一次。注意：已经在主线程发出，可直接更新 UI。
    void frameReady(const QImage &frame);
    void stateChanged(LiveWindowSource::State state, const QString &message);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    State m_state = State::Initializing;
    QString m_stateMessage;
    QRect m_windowRect;
    QImage m_lastFrame;
};
