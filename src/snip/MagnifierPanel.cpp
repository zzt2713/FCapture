#include "snip/MagnifierPanel.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace {
constexpr int kSize = 160;
constexpr int kOffset = 22;
} // namespace

MagnifierPanel::MagnifierPanel()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    resize(kSize, kSize);
    hide();
}

void MagnifierPanel::showPanel(const Data &data, const QPoint &anchorGlobal,
                               const QRect &avoidGlobal)
{
    m_data = data;

    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    // 默认光标右下，靠近边缘翻转
    QPoint pos = anchorGlobal + QPoint(kOffset, kOffset);
    if (pos.x() + kSize > avail.right())
        pos.setX(anchorGlobal.x() - kOffset - kSize);
    if (pos.y() + kSize > avail.bottom())
        pos.setY(anchorGlobal.y() - kOffset - kSize);

    // 和工具栏 / 信息面板重叠时，翻到左侧或上方
    QRect box(pos, QSize(kSize, kSize));
    if (!avoidGlobal.isEmpty() && box.intersects(avoidGlobal)) {
        const int leftX = avoidGlobal.left() - kSize - 8;
        const int above = avoidGlobal.top() - kSize - 8;
        if (leftX >= avail.left())
            pos.setX(leftX);
        else if (above >= avail.top())
            pos.setY(above);
    }

    pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - kSize)));
    pos.setY(qBound(avail.top(), pos.y(), qMax(avail.top(), avail.bottom() - kSize)));
    move(pos);

    if (!isVisible())
        show();
    raise();
    update();
}

void MagnifierPanel::hidePanel()
{
    if (isVisible())
        hide();
}

QRect MagnifierPanel::globalRect() const
{
    return isVisible() ? QRect(pos(), size()) : QRect();
}

void MagnifierPanel::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    const QRect box = rect();

    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // 内容
    p.fillRect(box, Qt::black);
    if (!m_data.sample.isNull())
        p.drawImage(box, m_data.sample);

    // 十字准心 / 中心像素框（只在还没选好区域时画）
    if (m_data.picking) {
        p.setPen(QPen(Theme::kGuideLine, 1));
        p.drawLine(box.left(), box.center().y(), box.right(), box.center().y());
        p.drawLine(box.center().x(), box.top(), box.center().x(), box.bottom());

        const int pxSize = qMax(2, m_data.pixelSize);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(box.center().x() - pxSize / 2, box.center().y() - pxSize / 2,
                         pxSize, pxSize));
    }

    // 外框
    p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
    p.drawRect(box.adjusted(0, 0, -1, -1));

    // 中心像素色块
    if (m_data.pixelValid) {
        const QRect swatch(box.left() + 4, box.bottom() - 18, 12, 12);
        p.setPen(QPen(Qt::white, 1));
        p.setBrush(m_data.pixelColor);
        p.drawRect(swatch);
    }

    // 顶部信息条
    QFont f = Theme::appFont();
    f.setPointSize(8);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int textW = qMin(fm.horizontalAdvance(m_data.infoText) + 12, width());
    const QRect infoBox(box.left(), box.top() - 20, textW, 18);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 200));
    p.drawRoundedRect(infoBox, 3, 3);
    p.setPen(QColor(0xE8, 0xE8, 0xE8));
    p.drawText(infoBox.adjusted(6, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
               m_data.infoText);
}
