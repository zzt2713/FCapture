#include "snip/LongShotBar.h"

#include "ui/Theme.h"
#include "ui/ToolIcons.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace {

const char *kTooltipTexts[] = {
    "开始拼接",
    "暂停拼接",
    "保存长图",
    "复制长图",
    "贴到屏幕",
    "关闭截图",
};

} // namespace

LongShotBar::LongShotBar()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    rebuildLayout();
}

void LongShotBar::rebuildLayout()
{
    // 分三组：开始/暂停 | 保存/复制/钉 | 关闭（组间空隙更大，和 PaintBar 一致）
    static const int kGroups[][3] = {
        {BtnStart, BtnPause, -1},
        {BtnSave, BtnCopy, BtnPin},
        {BtnClose, -1, -1},
    };

    int x = kPadding;
    for (const auto &group : kGroups) {
        for (int j = 0; j < 3 && group[j] >= 0; ++j) {
            m_items[group[j]].rect = QRect(x, kPadding, kButtonSize, kButtonSize);
            x += kButtonSize + kGap;
        }
        x += kSeparatorGap - kGap;
    }

    setFixedSize(x - kSeparatorGap + kGap + kPadding,
                 kPadding * 2 + kButtonSize + kTooltipArea);
}

void LongShotBar::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    update();
}

void LongShotBar::setHasContent(bool hasContent)
{
    if (m_hasContent == hasContent)
        return;
    m_hasContent = hasContent;
    update();
}

void LongShotBar::positionNear(const QRect &sel)
{
    QScreen *screen = QGuiApplication::screenAt(sel.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry()
                               : QRect(0, 0, 1920, 1080);

    const int x = qBound(avail.left() + 4, sel.center().x() - width() / 2,
                         avail.right() - width() - 4);
    int y = sel.bottom() + 12;
    if (y + height() > avail.bottom() - 4)
        y = sel.top() - height() - 12;
    if (y < avail.top() + 4)
        y = qMin(sel.bottom() + 12, avail.bottom() - height() - 4);
    move(x, y);
}

bool LongShotBar::buttonEnabled(int id) const
{
    switch (id) {
    case BtnStart: return !m_running;
    case BtnPause: return m_running;
    case BtnSave:
    case BtnCopy:
    case BtnPin: return m_hasContent;
    case BtnClose: return true;
    }
    return true;
}

int LongShotBar::hitButton(const QPoint &pos) const
{
    for (int i = 0; i < BtnCount; ++i) {
        if (m_items[i].rect.contains(pos))
            return i;
    }
    return -1;
}

void LongShotBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x26, 0x26, 0x26, 242));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);

    for (int i = 0; i < BtnCount; ++i) {
        const bool en = buttonEnabled(i);
        const bool hover = m_hover == i && en;
        const bool pressed = m_pressed == i && en;

        if (pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x33, 0x33, 0x33));
            p.drawRoundedRect(m_items[i].rect, 4, 4);
        } else if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::kHover);
            p.drawRoundedRect(m_items[i].rect, 4, 4);
        }

        // 置灰也不能太暗：0x5A 贴在深色底上几乎看不见，看着像"图标空白"
        const QColor iconColor = en ? QColor(0xE8, 0xE8, 0xE8) : QColor(0x9A, 0x9A, 0x9A);
        QPixmap icon;
        switch (i) {
        case BtnStart: icon = ToolIcons::playIcon(20, iconColor); break;
        case BtnPause: icon = ToolIcons::pauseIcon(20, iconColor); break;
        case BtnSave:  icon = ToolIcons::saveIcon(20, iconColor); break;
        case BtnCopy:  icon = ToolIcons::copyIcon(20, iconColor); break;
        case BtnPin:   icon = ToolIcons::pinIcon(20, iconColor); break;
        case BtnClose: icon = ToolIcons::closeIcon(20, iconColor); break;
        default: break;
        }
        if (!icon.isNull()) {
            p.drawPixmap(m_items[i].rect.center()
                             - QPoint(qRound(icon.width() / icon.devicePixelRatio() / 2),
                                      qRound(icon.height() / icon.devicePixelRatio() / 2)),
                         icon);
        }
    }

    // 底部自绘悬停提示（与 PaintBar 一致，不依赖系统 QToolTip）
    if (m_hover >= 0 && m_hover < BtnCount) {
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(QColor(0xBB, 0xBB, 0xBB));
        p.drawText(QRect(0, kPadding * 2 + kButtonSize, width(), kTooltipArea),
                   Qt::AlignCenter, QString::fromUtf8(kTooltipTexts[m_hover]));
    }
}

void LongShotBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const int i = hitButton(e->position().toPoint());
    m_pressed = (i >= 0 && buttonEnabled(i)) ? i : -1;
    update();
}

void LongShotBar::mouseMoveEvent(QMouseEvent *e)
{
    const int i = hitButton(e->position().toPoint());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
}

void LongShotBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        m_pressed = -1;
        update();
        return;
    }
    const int i = hitButton(e->position().toPoint());
    const int act = (i >= 0 && i == m_pressed) ? i : -1;
    m_pressed = -1;
    update();
    if (act < 0)
        return;

    switch (act) {
    case BtnStart: emit startRequested(); break;
    case BtnPause: emit pauseRequested(); break;
    case BtnSave: emit saveRequested(); break;
    case BtnCopy: emit copyRequested(); break;
    case BtnPin: emit pinRequested(); break;
    case BtnClose: emit closeRequested(); break;
    default: break;
    }
}

void LongShotBar::leaveEvent(QEvent *)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
}
