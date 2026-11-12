#include "ui/ShortcutKeyButton.h"

#include "ElaTheme.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

// snow metric：controlHeight = 32, borderRadius = 6
constexpr int kHeight = 32;
constexpr int kRadius = 6;
constexpr int kPaddingH = 12;
constexpr int kIconSize = 14;
constexpr int kIconGap = 8;

QColor themeColor(ElaThemeType::ThemeColor role)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), role);
}

} // namespace

ShortcutKeyButton::ShortcutKeyButton(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kHeight);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumWidth(0);
}

void ShortcutKeyButton::setShortcutText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    updateGeometry();
    update();
}

void ShortcutKeyButton::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    update();
}

QSize ShortcutKeyButton::sizeHint() const
{
    QFont f = font();
    f.setPixelSize(14);
    const QFontMetrics fm(f);
    // 录制中：没按到键显示"请按下按键…"，按到了就显示那个键（snow 行为）
    const QString display = !m_text.isEmpty()
                                ? m_text
                                : (m_state == State::Recording ? QStringLiteral("请按下按键…")
                                                               : QStringLiteral("未设置"));
    // 图标 + 间距 + 文字 + 左右内边距。
    // 末尾多留 2px：绘制时圆角边框会向内收 0.5px（见 paintEvent 的 adjusted），
    // 正好差 1px 时 elidedText 会把 "F1" 截成 "…"。
    return QSize(kPaddingH * 2 + kIconSize + kIconGap + fm.horizontalAdvance(display) + 2,
                 kHeight);
}

// 图标：snow 用的是 Keyboard()，这里画一个等效的键盘轮廓
void ShortcutKeyButton::paintGridIcon(QPainter &p, const QRectF &rect, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF body = rect.adjusted(0.5, rect.height() * 0.2, -0.5, -rect.height() * 0.2);
    p.setPen(QPen(color, 1.1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(body, 2, 2);

    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal stepX = (body.width() - 4.0) / 3.0;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 4; ++c) {
            p.drawRect(QRectF(body.left() + 2.0 + c * stepX,
                              body.top() + 2.2 + r * 2.6, 1.4, 1.4));
        }
    }
    p.restore();
}

void ShortcutKeyButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(box, kRadius, kRadius);

    // 底色
    QColor fill = Qt::transparent;
    if (m_state == State::Recording) {
        fill = themeColor(ElaThemeType::PrimaryNormal);
        fill.setAlpha(20);
    } else if (m_down) {
        fill = themeColor(ElaThemeType::BasicPress);
    } else if (m_hover) {
        fill = themeColor(ElaThemeType::BasicHover);
    }
    if (fill.alpha() > 0)
        p.fillPath(path, fill);

    // 边框
    QColor border;
    switch (m_state) {
    case State::Recording:
        border = themeColor(ElaThemeType::PrimaryNormal);
        break;
    case State::Invalid:
        border = QColor(0xFF, 0x4D, 0x4F);
        break;
    default:
        border = m_hover ? themeColor(ElaThemeType::PrimaryNormal)
                         : themeColor(ElaThemeType::BasicBorder);
        break;
    }
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // 文字颜色
    QColor fg;
    switch (m_state) {
    case State::Recording:
        fg = themeColor(ElaThemeType::PrimaryNormal);
        break;
    case State::Invalid:
        fg = QColor(0xFF, 0x4D, 0x4F);
        break;
    default:
        fg = m_text.isEmpty() ? themeColor(ElaThemeType::BasicDetailsText)
                              : themeColor(ElaThemeType::BasicText);
        break;
    }

    // 图标
    const qreal iconX = box.left() + kPaddingH;
    const QRectF iconRect(iconX, (height() - kIconSize) / 2.0, kIconSize, kIconSize);
    QColor iconColor = fg;
    if (m_state == State::Normal && !m_hover && !m_down)
        iconColor.setAlpha(qRound(iconColor.alpha() * 0.55));
    paintGridIcon(p, iconRect, iconColor);

    // 文字
    // 录制中：没按到键显示"请按下按键…"，按到了就显示那个键（snow 行为）
    const QString display = !m_text.isEmpty()
                                ? m_text
                                : (m_state == State::Recording ? QStringLiteral("请按下按键…")
                                                               : QStringLiteral("未设置"));
    QFont f = font();
    f.setPixelSize(14);
    p.setFont(f);
    p.setPen(fg);

    const QRectF textRect(iconRect.right() + kIconGap, box.top(),
                          box.width() - (iconRect.right() - box.left()) - kIconGap - kPaddingH,
                          box.height());
    const QFontMetrics fm(f);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(display, Qt::ElideRight, qRound(textRect.width())));
}

void ShortcutKeyButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_down = true;
        update();
    }
    QWidget::mousePressEvent(e);
}

void ShortcutKeyButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_down) {
        m_down = false;
        update();
        if (rect().contains(e->position().toPoint()))
            emit clicked();
    }
    QWidget::mouseReleaseEvent(e);
}

void ShortcutKeyButton::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QWidget::enterEvent(e);
}

void ShortcutKeyButton::leaveEvent(QEvent *e)
{
    m_hover = false;
    m_down = false;
    update();
    QWidget::leaveEvent(e);
}
