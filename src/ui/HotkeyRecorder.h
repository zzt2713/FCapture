#pragma once

#include "ElaLineEdit.h"

class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;

// 全局快捷键录制框。
//
// 代替 ElaKeyBinder：本项目自己处理按键事件并把结果规范化为
// HotkeyManager 能解析的字符串，避免 Ela 私有录制逻辑导致界面卡死。
class HotkeyRecorder final : public ElaLineEdit
{
    Q_OBJECT

public:
    explicit HotkeyRecorder(QWidget *parent = nullptr);

    // 更新当前显示的热键，不会触发 hotkeyRecorded。
    void setHotkeyText(const QString &hotkey);
    // 保留原热键作为有效绑定，但把输入框显示为失败提示。
    void showConflict(const QString &activeHotkey);
    QString hotkeyText() const;
    bool isRecording() const;
    void cancelRecording();

signals:
    void recordingStarted();
    void recordingFinished();
    void hotkeyRecorded(const QString &hotkey);
    void validationFailed(const QString &message);

protected:
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void beginRecording();

    bool m_recording = false;
    bool m_hasConflict = false;
    QString m_hotkeyText;
};
