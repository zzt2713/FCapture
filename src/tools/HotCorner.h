#pragma once

#include <QObject>
#include <QRect>

class QTimer;

// 屏幕热角：鼠标停在屏幕某个角 10×10 区域时触发动作。
//
// 防抖：触发后必须离开角落再进入才会再次触发。
class HotCorner : public QObject
{
    Q_OBJECT

public:
    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    // 与 config.ini 里的 hotcorner/<key> 对应
    enum class Action {
        None,
        HideAllPins,
        ShowAllPins,
        TogglePins,
        Capture,
        Paste,
    };

    explicit HotCorner(QObject *parent = nullptr);
    ~HotCorner() override;

    static QString cornerKey(Corner corner);
    static QString actionKey(Action action);
    static Action actionFromString(const QString &s);

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

signals:
    void actionTriggered(HotCorner::Action action);

private:
    void tick();
    void reloadSettings();

    QTimer *m_timer = nullptr;
    bool m_enabled = false;
    Action m_actions[4] = { Action::None, Action::None, Action::None, Action::None };
    bool m_inside[4] = { false, false, false, false };

    static constexpr int kZoneSize = 10;
    static constexpr int kIntervalMs = 200;
};
