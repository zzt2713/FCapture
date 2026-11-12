#include "ui/ShortcutRow.h"

#include "ui/ShortcutKeyButton.h"

#include "ElaTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

// snow metric：controlHeightLG(40) + paddingXXS(4) = 44
constexpr int kRowHeight = 44;
// mainWindowMetric.cardRadius = 10
constexpr int kRowRadius = 10;
constexpr int kPaddingH = 16;      // metric.padding
constexpr int kPaddingV = 4;       // metric.paddingXXS
constexpr int kTitleIconSize = 18; // fontSizeLG + borderRadiusXS

QColor themeColor(ElaThemeType::ThemeColor role)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), role);
}

} // namespace

ShortcutRow::ShortcutRow(const QString &title, const QString &action, QWidget *parent)
    : QWidget(parent)
    , m_action(action)
    , m_title(title)
{
    setFixedHeight(kRowHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(kPaddingH, kPaddingV, kPaddingH, kPaddingV);
    layout->setSpacing(12);

    // 左侧：标题
    m_titleLabel = new QLabel(title, this);
    QFont f = m_titleLabel->font();
    f.setPixelSize(14);
    m_titleLabel->setFont(f);
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_titleLabel, 1, Qt::AlignVCenter);

    // 右侧：一个键一个框，横向排列
    m_keyLayout = new QHBoxLayout;
    m_keyLayout->setContentsMargins(0, 0, 0, 0);
    m_keyLayout->setSpacing(8);
    layout->addLayout(m_keyLayout, 0);
}

void ShortcutRow::setShortcutKeys(const QStringList &keys)
{
    m_keys = keys;
    rebuildKeyButtons();
}

void ShortcutRow::rebuildKeyButtons()
{
    // 控件数量只增不减：键数量变化时复用已有控件，避免频繁 new/delete
    while (m_keyButtons.size() < m_keys.size()) {
        auto *button = new ShortcutKeyButton(this);
        connect(button, &ShortcutKeyButton::clicked, this, &ShortcutRow::clicked);
        m_keyLayout->addWidget(button, 0, Qt::AlignVCenter);
        m_keyButtons.append(button);
    }

    for (int i = 0; i < m_keyButtons.size(); ++i) {
        ShortcutKeyButton *button = m_keyButtons.at(i);
        if (i < m_keys.size()) {
            const QString key = m_keys.at(i);
            button->setShortcutText(key);
            button->setState(m_invalidKeys.contains(key, Qt::CaseInsensitive)
                                 ? ShortcutKeyButton::State::Invalid
                                 : ShortcutKeyButton::State::Normal);
        } else {
            button->hide();
        }
    }

    relayoutKeyButtons();
    updateGeometry();
    update();
}

// 行宽不够时从右往左隐藏多余的键：配置里的键仍然保留，
// 只是这一行显示不下（弹窗里能看到全部）。
void ShortcutRow::relayoutKeyButtons()
{
    if (m_keyButtons.isEmpty())
        return;

    const int titleWidth = m_titleLabel ? m_titleLabel->sizeHint().width() : 0;
    const int available = width() - kPaddingH * 2 - titleWidth - 12;
    const int spacing = m_keyLayout->spacing();

    // 放不下就从这个键开始全部隐藏：显示成"前 N 个"，
    // 而不是中间缺一个，看着像丢配置。
    int used = 0;
    bool overflow = false;
    for (int i = 0; i < m_keyButtons.size(); ++i) {
        ShortcutKeyButton *button = m_keyButtons.at(i);
        if (overflow || i >= m_keys.size()) {
            button->hide();
            continue;
        }
        const int buttonWidth = button->sizeHint().width();
        const int needed = used + (i > 0 ? spacing : 0) + buttonWidth;
        if (i > 0 && needed > available) {
            overflow = true;
            button->hide();
            continue;
        }
        button->show();
        used = needed;
    }
}

void ShortcutRow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayoutKeyButtons();
}

void ShortcutRow::setInvalidKeys(const QStringList &keys)
{
    m_invalidKeys = keys;
    for (int i = 0; i < m_keyButtons.size() && i < m_keys.size(); ++i) {
        m_keyButtons.at(i)->setState(
            keys.contains(m_keys.at(i), Qt::CaseInsensitive)
                ? ShortcutKeyButton::State::Invalid
                : ShortcutKeyButton::State::Normal);
    }
}

void ShortcutRow::setToolTipText(const QString &text)
{
    for (ShortcutKeyButton *button : m_keyButtons)
        button->setToolTip(text);
    setToolTip(text);
}

QSize ShortcutRow::sizeHint() const
{
    // 只按"标题 + 第一个键"报宽度：键的数量不参与，否则多键会把整页撑宽。
    // 实际能显示几个由 relayoutKeyButtons() 按真实行宽决定。
    const int titleWidth = m_titleLabel ? m_titleLabel->sizeHint().width() : 120;
    const int firstKeyWidth =
        m_keyButtons.isEmpty() ? 120 : m_keyButtons.first()->sizeHint().width();
    return QSize(kPaddingH * 2 + titleWidth + 12 + firstKeyWidth, kRowHeight);
}

QSize ShortcutRow::minimumSizeHint() const
{
    return QSize(kPaddingH * 2 + 120 + 12 + 120, kRowHeight);
}

void ShortcutRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // snow 的 ActionRow：Outline 风格，圆角 10 + 1px 描边
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(box, kRowRadius, kRowRadius);

    QColor fill = Qt::transparent;
    if (m_down)
        fill = themeColor(ElaThemeType::BasicPress);
    else if (m_hover)
        fill = themeColor(ElaThemeType::BasicHover);
    if (fill.alpha() > 0)
        p.fillPath(path, fill);

    p.setPen(QPen(m_hover || m_down ? themeColor(ElaThemeType::PrimaryNormal)
                                    : themeColor(ElaThemeType::BasicBorder),
                  1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ShortcutRow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_down = true;
        update();
    }
    QWidget::mousePressEvent(e);
}

void ShortcutRow::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_down) {
        m_down = false;
        update();
        if (rect().contains(e->position().toPoint()))
            emit clicked();
    }
    QWidget::mouseReleaseEvent(e);
}

void ShortcutRow::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QWidget::enterEvent(e);
}

void ShortcutRow::leaveEvent(QEvent *e)
{
    m_hover = false;
    m_down = false;
    update();
    QWidget::leaveEvent(e);
}
