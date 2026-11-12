#include "ui/ShortcutConfigWidget.h"

#include "ui/ShortcutKeyButton.h"

#include "ElaTheme.h"

#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {

constexpr int kRowGap = 12;
constexpr int kControlHeight = 32;
constexpr int kActionSize = 32;

QColor themeColor(ElaThemeType::ThemeColor role)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), role);
}

// snow 的 bindingFromKeyEvent 精简版：
// 用 QKeySequence(PortableText) 生成 "Ctrl+Alt+A" 这种格式
QString keyEventToPortable(const QKeyEvent &e)
{
    const int key = e.key();
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta) {
        return QString();   // 只按修饰键不算
    }
    const Qt::KeyboardModifiers mods =
        e.modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier
                         | Qt::MetaModifier);
    const QString text =
        QKeySequence(QKeyCombination(mods, static_cast<Qt::Key>(key)))
            .toString(QKeySequence::PortableText)
            .trimmed();
    return text;
}

// 行尾操作按钮：删除=红框垃圾桶，确认=绿框对勾
class RowActionButton final : public QWidget
{
public:
    enum class Kind { Delete, Confirm };

    RowActionButton(Kind kind, QWidget *parent)
        : QWidget(parent), m_kind(kind)
    {
        setFixedSize(kActionSize, kActionSize);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const bool danger = (m_kind == Kind::Delete);
        const QColor accent = danger ? QColor(0xFF, 0x4D, 0x4F) : QColor(0x52, 0xC4, 0x1A);

        QColor fill = Qt::transparent;
        if (!isEnabled())
            fill = Qt::transparent;
        else if (m_down)
            fill = QColor(accent.red(), accent.green(), accent.blue(), 32);
        else if (m_hover)
            fill = QColor(accent.red(), accent.green(), accent.blue(), 18);

        const QColor lineColor = isEnabled() ? accent : themeColor(ElaThemeType::BasicDisable);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(box, 6, 6);
        if (fill.alpha() > 0)
            p.fillPath(path, fill);
        p.setPen(QPen(lineColor, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 6, 6);

        p.setPen(QPen(lineColor, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF c = box.center();
        if (danger) {
            // 垃圾桶
            const qreal w = 5.5;
            p.drawLine(QPointF(c.x() - w, c.y() - 3.2), QPointF(c.x() + w, c.y() - 3.2));
            p.drawLine(QPointF(c.x() - 2.2, c.y() - 3.2), QPointF(c.x() - 1.6, c.y() - 5.0));
            p.drawLine(QPointF(c.x() + 2.2, c.y() - 3.2), QPointF(c.x() + 1.6, c.y() - 5.0));
            p.drawLine(QPointF(c.x() - 1.6, c.y() - 5.0), QPointF(c.x() + 1.6, c.y() - 5.0));
            p.drawLine(QPointF(c.x() - w + 0.8, c.y() - 3.2),
                       QPointF(c.x() - w + 1.6, c.y() + 4.4));
            p.drawLine(QPointF(c.x() + w - 0.8, c.y() - 3.2),
                       QPointF(c.x() + w - 1.6, c.y() + 4.4));
            p.drawLine(QPointF(c.x() - w + 1.6, c.y() + 4.4),
                       QPointF(c.x() + w - 1.6, c.y() + 4.4));
        } else {
            p.drawLine(QPointF(c.x() - 4.5, c.y()), QPointF(c.x() - 1, c.y() + 3.5));
            p.drawLine(QPointF(c.x() - 1, c.y() + 3.5), QPointF(c.x() + 4.5, c.y() - 3.5));
        }
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; m_down = false; update(); }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_down = true;
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_down) {
            m_down = false;
            update();
            if (isEnabled() && rect().contains(e->position().toPoint()) && onClick)
                onClick();
        }
    }

private:
    Kind m_kind;
    bool m_hover = false;
    bool m_down = false;
};

// 「＋ 添加按键配置」：蓝色虚线框 + 蓝色文字
class AddKeyButton final : public QWidget
{
public:
    explicit AddKeyButton(QWidget *parent) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QColor accent = themeColor(ElaThemeType::PrimaryNormal);
        QColor fill = Qt::transparent;
        if (m_hover) {
            fill = accent;
            fill.setAlpha(16);
        }

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(box, 6, 6);
        if (fill.alpha() > 0)
            p.fillPath(path, fill);

        QPen pen(accent, 1.2);
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({ 3.0, 3.0 });
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box, 6, 6);

        QFont f = font();
        f.setPixelSize(14);
        p.setFont(f);
        p.setPen(accent);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("＋  添加按键配置"));
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick)
            onClick();
    }

private:
    bool m_hover = false;
};

} // namespace

ShortcutConfigWidget::ShortcutConfigWidget(const QStringList &keys, QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kRowGap);

    m_rowLayout = new QVBoxLayout;
    m_rowLayout->setContentsMargins(0, 0, 0, 0);
    m_rowLayout->setSpacing(kRowGap);
    root->addLayout(m_rowLayout);

    auto *addButton = new AddKeyButton(this);
    addButton->setFixedHeight(kControlHeight);
    addButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addButton->onClick = [this]() { addConfig(); };
    m_addButton = addButton;
    root->addWidget(m_addButton);

    // 初始化：过滤空键
    for (const QString &key : keys) {
        if (m_configs.size() >= m_maxKeys)
            break;
        if (!key.trimmed().isEmpty())
            m_configs.append({ key, m_nextConfigIndex++ });
    }
    if (m_configs.isEmpty())
        m_configs.append({ QString(), m_nextConfigIndex++ });

    // snow：只有一个空键时自动进入录制
    if (m_configs.size() == 1 && m_configs.first().key.isEmpty())
        m_recordingIndex = m_configs.first().index;

    rebuildRows();
}

void ShortcutConfigWidget::setMaxKeys(int max)
{
    m_maxKeys = qMax(1, max);
    while (m_configs.size() > m_maxKeys)
        m_configs.removeLast();
    rebuildRows();
}

void ShortcutConfigWidget::setValidator(std::function<QString(const QString &key)> validator)
{
    m_validator = std::move(validator);
}

QStringList ShortcutConfigWidget::keys() const
{
    QStringList out;
    for (const KeyConfig &c : m_configs) {
        const QString k = (c.index == m_recordingIndex && !m_pendingKey.isEmpty())
                              ? m_pendingKey
                              : c.key;
        if (!k.trimmed().isEmpty() && !out.contains(k, Qt::CaseInsensitive))
            out.append(k);
    }
    return out;
}

bool ShortcutConfigWidget::isRecording() const
{
    return m_recordingIndex >= 0;
}

bool ShortcutConfigWidget::canAccept() const
{
    return m_recordingIndex < 0 || !m_pendingKey.isEmpty();
}

void ShortcutConfigWidget::commitPending()
{
    if (m_recordingIndex >= 0 && !m_pendingKey.isEmpty())
        applyPending();
}

void ShortcutConfigWidget::stopRecording()
{
    m_recordingIndex = -1;
    m_pendingKey.clear();
    m_validationMessage.clear();
    rebuildRows();
}

ShortcutConfigWidget::KeyConfig *ShortcutConfigWidget::findConfig(int configIndex)
{
    for (KeyConfig &c : m_configs) {
        if (c.index == configIndex)
            return &c;
    }
    return nullptr;
}

void ShortcutConfigWidget::rebuildRows()
{
    // 必须 deleteLater，不能同步 delete：
    // 点「确定」「删除」或某个键按钮都会在**该按钮自己的 mouseReleaseEvent**
    // 栈上走到这里（applyPending / deleteConfig / startRecording → rebuildRows）。
    // 同步 delete 会把正在派发事件的控件删掉，事件返回后 Qt 继续访问它
    // （QWidget::testAttribute_helper 处 0xC0000005），表现就是闪退。
    while (QLayoutItem *item = m_rowLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    for (const KeyConfig &config : m_configs) {
        const bool recording = (config.index == m_recordingIndex);

        auto *row = new QWidget(this);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *keyButton = new ShortcutKeyButton(row);
        keyButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (recording) {
            keyButton->setState(m_validationMessage.isEmpty() ? ShortcutKeyButton::State::Recording
                                                              : ShortcutKeyButton::State::Invalid);
            keyButton->setShortcutText(m_pendingKey);
            keyButton->setToolTip(m_validationMessage);
        } else {
            keyButton->setState(ShortcutKeyButton::State::Normal);
            keyButton->setShortcutText(config.key);
            keyButton->setToolTip(QString());
        }
        connect(keyButton, &ShortcutKeyButton::clicked, this,
                [this, idx = config.index]() { startRecording(idx); });

        auto *actionButton = new RowActionButton(
            recording ? RowActionButton::Kind::Confirm : RowActionButton::Kind::Delete, row);
        actionButton->setToolTip(recording ? QStringLiteral("确认这个快捷键")
                                           : QStringLiteral("删除这个快捷键"));
        if (recording) {
            actionButton->setEnabled(!m_pendingKey.isEmpty());
            actionButton->onClick = [this]() { applyPending(); };
        } else {
            actionButton->onClick = [this, idx = config.index]() { deleteConfig(idx); };
        }

        rowLayout->addWidget(keyButton, 1);
        rowLayout->addWidget(actionButton, 0);

        m_rowLayout->addWidget(row);
    }

    syncAddButton();
    emit stateChanged();
}

void ShortcutConfigWidget::addConfig()
{
    if (m_configs.size() >= m_maxKeys)
        return;

    // 当前行还在录制 → 先结束（有 pending 就保留）
    if (m_recordingIndex >= 0) {
        if (!m_pendingKey.isEmpty())
            applyPending();
        else
            stopRecording();
    }

    m_configs.append({ QString(), m_nextConfigIndex++ });
    startRecording(m_configs.last().index);
}

void ShortcutConfigWidget::deleteConfig(int configIndex)
{
    if (m_recordingIndex == configIndex)
        stopRecording();

    for (int i = 0; i < m_configs.size(); ++i) {
        if (m_configs.at(i).index == configIndex) {
            m_configs.removeAt(i);
            break;
        }
    }
    if (m_configs.isEmpty())
        m_configs.append({ QString(), m_nextConfigIndex++ });

    rebuildRows();
}

void ShortcutConfigWidget::startRecording(int configIndex)
{
    if (findConfig(configIndex) == nullptr)
        return;

    m_recordingIndex = configIndex;
    m_pendingKey.clear();
    m_validationMessage.clear();
    rebuildRows();
    setFocus(Qt::OtherFocusReason);
}

void ShortcutConfigWidget::applyPending()
{
    if (m_recordingIndex < 0 || m_pendingKey.isEmpty())
        return;
    if (KeyConfig *config = findConfig(m_recordingIndex))
        config->key = m_pendingKey;
    stopRecording();
}

void ShortcutConfigWidget::syncAddButton()
{
    if (m_addButton == nullptr)
        return;
    // snow 的 syncAddButtonState：达上限隐藏；录制中禁用。
    // 但这里放宽成"录制中也允许点"——点了会先把当前行收尾再加新行，
    // 否则用户第一行还在录的时候点添加会以为按钮坏了。
    const bool canAddMore = m_configs.size() < m_maxKeys;
    m_addButton->setVisible(canAddMore);
    m_addButton->setEnabled(canAddMore);
}

void ShortcutConfigWidget::forwardKeyPress(QKeyEvent *e)
{
    if (m_recordingIndex < 0)
        return;
    if (e->isAutoRepeat())
        return;
    if (e->key() == Qt::Key_Escape) {
        stopRecording();
        return;
    }

    const QString hotkey = keyEventToPortable(*e);
    if (hotkey.isEmpty())
        return;   // 只按了修饰键，继续等

    // 同一动作内重复
    for (const KeyConfig &c : m_configs) {
        if (c.index != m_recordingIndex && c.key.compare(hotkey, Qt::CaseInsensitive) == 0) {
            m_validationMessage = QStringLiteral("这个快捷键已经在列表里了");
            rebuildRows();
            return;
        }
    }

    // 跨动作冲突
    if (m_validator) {
        const QString reason = m_validator(hotkey);
        if (!reason.isEmpty()) {
            m_validationMessage = reason;
            rebuildRows();
            return;
        }
    }

    // 存 pending，等点绿色 ✓ 才写入（snow 的 m_pendingShortcut）
    m_pendingKey = hotkey;
    m_validationMessage.clear();
    rebuildRows();
}

void ShortcutConfigWidget::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
}
