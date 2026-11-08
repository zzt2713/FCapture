#include "ui/HotkeyRecorder.h"

#include "core/HotkeyManager.h"

#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStringList>

namespace {

const QString kHotkeyConflictText = QStringLiteral("快捷键被占用");

bool isModifierKey(int key)
{
    return key == Qt::Key_Control
           || key == Qt::Key_Shift
           || key == Qt::Key_Alt
           || key == Qt::Key_Meta;
}

QString modifierText(Qt::KeyboardModifiers modifiers)
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier))
        parts.append(QStringLiteral("Ctrl"));
    if (modifiers.testFlag(Qt::AltModifier))
        parts.append(QStringLiteral("Alt"));
    if (modifiers.testFlag(Qt::ShiftModifier))
        parts.append(QStringLiteral("Shift"));
    if (modifiers.testFlag(Qt::MetaModifier))
        parts.append(QStringLiteral("Win"));
    return parts.join(QLatin1Char('+'));
}

QString mainKeyText(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(static_cast<ushort>(key)));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(static_cast<ushort>(key)));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);

    switch (key) {
    case Qt::Key_Print:
        return QStringLiteral("Print");
    case Qt::Key_Insert:
        return QStringLiteral("Insert");
    case Qt::Key_Delete:
        return QStringLiteral("Delete");
    case Qt::Key_Home:
        return QStringLiteral("Home");
    case Qt::Key_End:
        return QStringLiteral("End");
    case Qt::Key_PageUp:
        return QStringLiteral("PageUp");
    case Qt::Key_PageDown:
        return QStringLiteral("PageDown");
    case Qt::Key_Space:
        return QStringLiteral("Space");
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return QStringLiteral("Tab");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Enter");
    case Qt::Key_Backspace:
        return QStringLiteral("Backspace");
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    case Qt::Key_Up:
        return QStringLiteral("Up");
    case Qt::Key_Down:
        return QStringLiteral("Down");

    // 不区分 Shift 后的符号，统一还原为物理键位，修饰键由事件自身提供。
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return QStringLiteral("-");
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        return QStringLiteral("=");
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return QStringLiteral("[");
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return QStringLiteral("]");
    case Qt::Key_Semicolon:
    case Qt::Key_Colon:
        return QStringLiteral(";");
    case Qt::Key_Apostrophe:
    case Qt::Key_QuoteDbl:
        return QStringLiteral("'");
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return QStringLiteral(",");
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return QStringLiteral(".");
    case Qt::Key_Slash:
    case Qt::Key_Question:
        return QStringLiteral("/");
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return QStringLiteral("\\");
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return QStringLiteral("`");

    case Qt::Key_Exclam:
        return QStringLiteral("1");
    case Qt::Key_At:
        return QStringLiteral("2");
    case Qt::Key_NumberSign:
        return QStringLiteral("3");
    case Qt::Key_Dollar:
        return QStringLiteral("4");
    case Qt::Key_Percent:
        return QStringLiteral("5");
    case Qt::Key_AsciiCircum:
        return QStringLiteral("6");
    case Qt::Key_Ampersand:
        return QStringLiteral("7");
    case Qt::Key_Asterisk:
        return QStringLiteral("8");
    case Qt::Key_ParenLeft:
        return QStringLiteral("9");
    case Qt::Key_ParenRight:
        return QStringLiteral("0");
    default:
        return QString();
    }
}

} // namespace

HotkeyRecorder::HotkeyRecorder(QWidget *parent)
    : ElaLineEdit(parent)
{
    setReadOnly(true);
    setFocusPolicy(Qt::StrongFocus);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("点击后按下新的快捷键，Esc 取消"));
}

void HotkeyRecorder::setHotkeyText(const QString &hotkey)
{
    m_hotkeyText = hotkey;
    m_hasConflict = false;
    setToolTip(QStringLiteral("点击后按下新的快捷键，Esc 取消"));
    if (!m_recording)
        setText(hotkey);
}

void HotkeyRecorder::showConflict(const QString &activeHotkey)
{
    if (!activeHotkey.isEmpty())
        m_hotkeyText = activeHotkey;
    m_recording = false;
    m_hasConflict = true;
    setText(kHotkeyConflictText);
    // 让用户知道到底是哪个组合被占用，而不是只看到一句提示
    setToolTip(activeHotkey.isEmpty()
                   ? kHotkeyConflictText
                   : QStringLiteral("「%1」已被其他程序占用，点击重新录制").arg(activeHotkey));
}

QString HotkeyRecorder::hotkeyText() const
{
    return m_hotkeyText;
}

bool HotkeyRecorder::isRecording() const
{
    return m_recording;
}

void HotkeyRecorder::cancelRecording()
{
    const bool wasRecording = m_recording;
    m_recording = false;

    // 取消录制 = 放弃这次输入，界面必须回到"当前真实状态"。
    // 真实状态有两种：仍然被占用（继续显示提示），或原绑定可用（显示按键）。
    // 之前不管冲突状态一律 setText(m_hotkeyText)，用户点一下再点走，
    // 输入框就从"快捷键被占用"变回 F3，看起来像已经被占用的键注册成功了。
    setText(m_hasConflict ? kHotkeyConflictText : m_hotkeyText);
    if (wasRecording)
        emit recordingFinished();
}

void HotkeyRecorder::focusInEvent(QFocusEvent *event)
{
    ElaLineEdit::focusInEvent(event);
    beginRecording();
}

void HotkeyRecorder::focusOutEvent(QFocusEvent *event)
{
    if (m_recording)
        cancelRecording();
    ElaLineEdit::focusOutEvent(event);
}

void HotkeyRecorder::hideEvent(QHideEvent *event)
{
    cancelRecording();
    ElaLineEdit::hideEvent(event);
}

void HotkeyRecorder::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 已经处于冲突态时，点击只是重新开始录制；一旦失焦又原样还原成
        // "快捷键被占用"，不会因为点了一下就把冲突提示抹掉。
        setFocus(Qt::MouseFocusReason);
        beginRecording();
        event->accept();
        return;
    }
    ElaLineEdit::mousePressEvent(event);
}

void HotkeyRecorder::keyPressEvent(QKeyEvent *event)
{
    if (!m_recording) {
        ElaLineEdit::keyPressEvent(event);
        return;
    }

    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        cancelRecording();
        event->accept();
        return;
    }

    if (isModifierKey(event->key())) {
        event->accept();
        return;
    }

    const QString key = mainKeyText(event->key());
    if (key.isEmpty()) {
        cancelRecording();
        emit validationFailed(QStringLiteral("该按键暂不支持，已保留原值"));
        event->accept();
        return;
    }

    const QString prefix = modifierText(event->modifiers());
    const QString hotkey = prefix.isEmpty()
                               ? key
                               : prefix + QLatin1Char('+') + key;

    quint32 modifiers = 0;
    quint32 virtualKey = 0;
    if (!HotkeyManager::parseKey(hotkey, &modifiers, &virtualKey)) {
        cancelRecording();
        emit validationFailed(QStringLiteral("该组合暂不支持，已保留原值"));
        event->accept();
        return;
    }

    // 候选键只用于显示和注册请求。注册成功前不改 m_hotkeyText，
    // 这样冲突、焦点丢失或取消时都能可靠恢复原绑定。
    m_recording = false;
    m_hasConflict = false;
    setToolTip(QStringLiteral("点击后按下新的快捷键，Esc 取消"));
    setText(hotkey);
    emit recordingFinished();
    emit hotkeyRecorded(hotkey);
    event->accept();
}

void HotkeyRecorder::beginRecording()
{
    if (m_recording)
        return;
    m_recording = true;
    setText(QStringLiteral("请按快捷键…"));
    emit recordingStarted();
}
