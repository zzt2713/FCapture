#include "snip/InfoPanel.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace {
constexpr int kPad = 10;
constexpr int kLineGap = 4;
constexpr int kSwatchSize = 14;
constexpr int kSwatchGap = 7;
} // namespace

InfoPanel::InfoPanel()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void InfoPanel::showInfo(const Data &data, const QPoint &anchorGlobal,
                         const QRect &avoidGlobal)
{
    m_data = data;

    QFont f = Theme::appFont();
    f.setPointSize(9);
    const QFontMetrics fm(f);
    const int lineH = fm.height();

    const int textWidth = qMax(fm.horizontalAdvance(data.positionText),
                               qMax(kSwatchSize + kSwatchGap + fm.horizontalAdvance(data.colorText),
                                    qMax(fm.horizontalAdvance(data.copyHint),
                                         fm.horizontalAdvance(data.formatHint))));
    m_panelSize = QSize(textWidth + kPad * 2, lineH * 4 + kLineGap * 3 + kPad * 2);
    resize(m_panelSize);

    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // 跟随鼠标：默认放在指针**正上方**（显示在取色/放大图上方），
    // 水平居中于指针，不遮挡当前悬停的像素点
    QPoint pos(anchorGlobal.x() - width() / 2, anchorGlobal.y() - height() - 20);

    // 边界碰撞：顶部越界 → 翻到鼠标下方；左右越界 → 夹紧到屏幕内
    if (pos.y() < avail.top())
        pos.setY(anchorGlobal.y() + 20);
    if (pos.x() < avail.left())
        pos.setX(avail.left() + 4);
    if (pos.x() + width() > avail.right())
        pos.setX(avail.right() - width() - 4);

    // 与工具栏重叠时再让一步（工具栏是独立子控件，面板必须避开它）
    if (!avoidGlobal.isEmpty()) {
        QRect box(pos, m_panelSize);
        if (box.intersects(avoidGlobal)) {
            const int above = avoidGlobal.top() - height() - 6;
            const int below = avoidGlobal.bottom() + 6;
            if (above >= avail.top())
                pos.setY(above);
            else if (below + height() <= avail.bottom())
                pos.setY(below);
            else
                pos.setX(qBound(avail.left(), avoidGlobal.left() - width() - 6,
                                qMax(avail.left(), avail.right() - width())));
        }
    }

    pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - width())));
    pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - height())));
    move(pos);

    if (!isVisible())
        show();
    raise();
    update();
}

void InfoPanel::showAbove(const Data &data, const QRect &attachGlobal)
{
    m_data = data;

    QFont f = Theme::appFont();
    f.setPointSize(9);
    const QFontMetrics fm(f);
    const int lineH = fm.height();
    const int textWidth = qMax(fm.horizontalAdvance(data.positionText),
                               qMax(kSwatchSize + kSwatchGap + fm.horizontalAdvance(data.colorText),
                                    qMax(fm.horizontalAdvance(data.copyHint),
                                         fm.horizontalAdvance(data.formatHint))));
    m_panelSize = QSize(textWidth + kPad * 2, lineH * 4 + kLineGap * 3 + kPad * 2);
    resize(m_panelSize);

    QScreen *screen = QGuiApplication::screenAt(attachGlobal.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    QPoint pos(attachGlobal.left(), attachGlobal.top() - height() - 2);
    if (pos.y() < avail.top())
        pos.setY(attachGlobal.bottom() + 2);

    pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - width())));
    pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - height())));
    move(pos);

    if (!isVisible())
        show();
    raise();
    update();
}

void InfoPanel::hideInfo()
{
    if (isVisible())
        hide();
}

QRect InfoPanel::globalRect() const
{
    return isVisible() ? QRect(pos(), size()) : QRect();
}

void InfoPanel::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont f = Theme::appFont();
    f.setPointSize(9);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int lineH = fm.height();
    const int textWidth = width() - kPad * 2;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2B, 0x2B, 0x2B, 238));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

    p.setPen(QColor(0xF2, 0xF2, 0xF2));
    int lineTop = kPad;

    p.drawText(QRect(kPad, lineTop, textWidth, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, m_data.positionText);

    lineTop += lineH + kLineGap;
    if (m_data.swatch.isValid()) {
        const QRect swatch(kPad, lineTop + (lineH - kSwatchSize) / 2,
                           kSwatchSize, kSwatchSize);
        p.setPen(QPen(QColor(0xE6, 0xE6, 0xE6), 1));
        p.setBrush(m_data.swatch);
        p.drawRect(swatch);
        p.setPen(QColor(0xF2, 0xF2, 0xF2));
        p.drawText(QRect(swatch.right() + 1 + kSwatchGap, lineTop,
                         textWidth - kSwatchSize - kSwatchGap, lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, m_data.colorText);
    }

    lineTop += lineH + kLineGap;
    p.drawText(QRect(kPad, lineTop, textWidth, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, m_data.copyHint);

    lineTop += lineH + kLineGap;
    p.drawText(QRect(kPad, lineTop, textWidth, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, m_data.formatHint);
}
