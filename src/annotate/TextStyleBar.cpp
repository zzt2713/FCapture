#include "annotate/TextStyleBar.h"

#include "annotate/shapes/TextShape.h"
#include "ui/ElaColorPickerDialog.h"
#include "ui/ToolIcons.h"

#include <QMouseEvent>
#include <QPainter>

namespace {
// 与 PaintBar 一致的浅色主题
constexpr QRgb kBarBg = 0xF7F7F7;
constexpr QRgb kBarBorder = 0xD8D8D8;
constexpr QRgb kIcon = 0x2B2B2B;
constexpr QRgb kHover = 0xE8E8E8;
constexpr QRgb kPress = 0xDCDCDC;
constexpr QRgb kDisabled = 0xA8A8A8;
} // namespace

TextStyleBar::TextStyleBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);

    const int n = int(Btn::Count);
    setFixedSize(kPadding * 2 + n * kBtnSize + (n - 1) * kGap, kPadding * 2 + kBtnSize);
    hide();
}

void TextStyleBar::setTarget(TextShape *shape)
{
    m_target = shape;
    if (!shape) {
        hide();
        return;
    }
    update();
}

void TextStyleBar::setEditingSelection(int start, int end, bool active)
{
    m_editSelActive = active && (end > start);
    m_editSelStart = start;
    m_editSelEnd = end;

    if (m_target && m_editSelActive) {
        // 编辑中：位置跟随选中文字的包围盒
        const QString before = m_target->text().left(start);
        const QString picked = m_target->text().mid(start, end - start);
        const QFontMetricsF fm(m_target->font());
        const QRectF box = m_target->layoutRect();
        Q_UNUSED(fm);
        Q_UNUSED(before);
        Q_UNUSED(picked);
        positionAbove(box.toRect());
    }
    update();
}

QRect TextStyleBar::buttonRect(Btn b) const
{
    const int i = int(b);
    return QRect(kPadding + i * (kBtnSize + kGap), kPadding, kBtnSize, kBtnSize);
}

TextStyleBar::Btn TextStyleBar::buttonAt(const QPoint &pos) const
{
    for (int i = 0; i < int(Btn::Count); ++i) {
        if (buttonRect(Btn(i)).contains(pos))
            return Btn(i);
    }
    return Btn::Count;
}

void TextStyleBar::positionAbove(const QRect &targetLocalRect)
{
    if (!parentWidget())
        return;
    const QRect avail = parentWidget()->rect();
    constexpr int kGapY = 8;

    int x = targetLocalRect.center().x() - width() / 2;
    x = qBound(avail.left() + 4, x, qMax(avail.left() + 4, avail.right() - width() - 4));

    int y = targetLocalRect.top() - height() - kGapY;
    if (y < avail.top() + 4)
        y = targetLocalRect.bottom() + kGapY;   // 上方放不下就放下方

    move(x, y);
    show();
    raise();
}

void TextStyleBar::applyFontDelta(int delta)
{
    if (!m_target)
        return;
    m_target->setFontSize(m_target->fontSize() + delta);
    emit styleChanged();
    update();
}

void TextStyleBar::pickColor()
{
    if (!m_target)
        return;
    const QColor chosen = ElaColorPickerDialog::getColor(
        m_target->color(), this, QStringLiteral("文字颜色"), true);
    if (!chosen.isValid())
        return;

    // 编辑中且有选区：只改选中部分
    if (m_editSelActive) {
        emit selectionColorRequested(chosen);
        return;
    }

    m_target->setColor(chosen);
    emit styleChanged();
    update();
}

void TextStyleBar::toggleBackground()
{
    if (!m_target)
        return;

    // 编辑中且有选区：高亮选中部分（而不是整段背景）
    if (m_editSelActive) {
        const QColor chosen = ElaColorPickerDialog::getColor(
            QColor(0xFF, 0xEB, 0x3B, 150), this, QStringLiteral("高亮颜色"), true);
        if (chosen.isValid())
            emit selectionHighlightRequested(chosen);
        return;
    }

    if (m_target->backgroundColor().alpha() > 0) {
        m_target->setBackgroundColor(Qt::transparent);
    } else {
        const QColor chosen = ElaColorPickerDialog::getColor(
            QColor(0, 0, 0, 160), this, QStringLiteral("背景填充色"), true);
        if (!chosen.isValid())
            return;
        m_target->setBackgroundColor(chosen);
    }
    emit styleChanged();
    update();
}

void TextStyleBar::toggleOutline()
{
    if (!m_target)
        return;
    if (m_target->outline()) {
        m_target->setOutline(false);
    } else {
        const QColor chosen = ElaColorPickerDialog::getColor(
            m_target->outlineColor(), this, QStringLiteral("描边颜色"));
        if (!chosen.isValid())
            return;
        m_target->setOutline(true);
        m_target->setOutlineColor(chosen);
    }
    emit styleChanged();
    update();
}

void TextStyleBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const Btn b = buttonAt(e->position().toPoint());
    if (b == Btn::Count)
        return;
    m_pressed = int(b);
    update();
}

void TextStyleBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        m_pressed = -1;
        update();
        return;
    }
    const Btn b = buttonAt(e->position().toPoint());
    const bool hit = (int(b) == m_pressed);
    m_pressed = -1;
    update();

    if (!hit || !m_target)
        return;

    switch (b) {
    case Btn::Color:       pickColor();       break;
    case Btn::FontSmaller: applyFontDelta(-2); break;
    case Btn::FontLarger:  applyFontDelta(+2); break;
    case Btn::Bg:          toggleBackground(); break;
    case Btn::Outline:     toggleOutline();    break;
    case Btn::Highlight: {
        if (m_editSelActive) {
            const QColor chosen = ElaColorPickerDialog::getColor(
                QColor(0xFF, 0xEB, 0x3B, 150), this, QStringLiteral("高亮颜色"), true);
            if (chosen.isValid())
                emit selectionHighlightRequested(chosen);
        } else if (m_target) {
            // 非编辑态：给整段加/去高亮
            m_target->applyHighlight(0, m_target->text().size(),
                                     m_target->runs().isEmpty()
                                         ? QColor(0xFF, 0xEB, 0x3B, 150)
                                         : QColor(Qt::transparent));
            emit styleChanged();
            update();
        }
        break;
    }
    case Btn::Count:       break;
    }
}

void TextStyleBar::mouseMoveEvent(QMouseEvent *e)
{
    const Btn b = buttonAt(e->position().toPoint());
    const int h = (b == Btn::Count) ? -1 : int(b);
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void TextStyleBar::leaveEvent(QEvent *e)
{
    Q_UNUSED(e);
    m_hover = -1;
    update();
}

void TextStyleBar::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背板
    p.setPen(QPen(QColor(kBarBorder), 1));
    p.setBrush(QColor(kBarBg));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    const bool hasTarget = (m_target != nullptr);

    for (int i = 0; i < int(Btn::Count); ++i) {
        const QRect r = buttonRect(Btn(i));
        const bool hover = (i == m_hover) && hasTarget;
        const bool pressed = (i == m_pressed) && hasTarget;

        if (pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kPress));
            p.drawRoundedRect(r, 4, 4);
        } else if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(kHover));
            p.drawRoundedRect(r, 4, 4);
        }

        const QColor c = hasTarget ? QColor(kIcon) : QColor(kDisabled);

        switch (Btn(i)) {
        case Btn::Color: {
            // 当前文字颜色色块
            const QColor cur = hasTarget ? m_target->color() : QColor(kDisabled);
            const QRect sw(r.center().x() - kSwatchSize / 2, r.center().y() - kSwatchSize / 2,
                           kSwatchSize, kSwatchSize);
            p.setPen(QPen(QColor(kBarBorder), 1));
            p.setBrush(cur);
            p.drawEllipse(sw);
            break;
        }
        case Btn::FontSmaller:
        case Btn::FontLarger: {
            // 用「A-」「A+」文字表示，比图标更直观
            QFont f = font();
            f.setPixelSize(Btn(i) == Btn::FontSmaller ? 11 : 13);
            f.setBold(true);
            p.setFont(f);
            p.setPen(c);
            p.drawText(r, Qt::AlignCenter,
                       Btn(i) == Btn::FontSmaller ? QStringLiteral("A−")
                                                  : QStringLiteral("A+"));
            break;
        }
        case Btn::Bg: {
            const bool on = hasTarget && m_target->backgroundColor().alpha() > 0;
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x3D, 0x8B, 0xFD));
                p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 3, 3);
            }
            QFont f = font();
            f.setPixelSize(11);
            p.setFont(f);
            p.setPen(on ? QColor(Qt::white) : c);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("Bg"));
            break;
        }
        case Btn::Outline: {
            const bool on = hasTarget && m_target->outline();
            if (on) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x3D, 0x8B, 0xFD));
                p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 3, 3);
            }
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(on ? QColor(Qt::white) : c);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("O"));
            break;
        }
        case Btn::Highlight: {
            // 高亮按钮：黄底 H 字
            QFont f = font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(c);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("H"));
            break;
        }
        case Btn::Count:
            break;
        }
    }
}
