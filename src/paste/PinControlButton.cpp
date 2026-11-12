#include "paste/PinControlButton.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

// 对齐 snow 的 ThemeManager 语义：
//   colorBgMask          → QColor(0, 0, 0, 115)
//   colorPrimary / Active → 主色悬停/按下
//   colorError / Active   → 红色悬停/按下
constexpr QRgb kMask          = qRgba(0, 0, 0, 115);
constexpr QRgb kPrimary       = qRgba(0x1E, 0x88, 0xE5, 255);
constexpr QRgb kPrimaryActive = qRgba(0x10, 0x6D, 0xC2, 255);
constexpr QRgb kError         = qRgba(0xE5, 0x3E, 0x3E, 255);
constexpr QRgb kErrorActive   = qRgba(0xC0, 0x2A, 0x2A, 255);

constexpr int kIconSize = 18;

} // namespace

PinControlButton::PinControlButton(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
{
    setFixedSize(kSize, kSize);
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setToolTip(kind == Kind::Edit ? QStringLiteral("重新标注")
                                  : QStringLiteral("关闭贴图"));
}

void PinControlButton::setButtonOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
}

void PinControlButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(m_opacity);

    QColor background = QColor::fromRgba(kMask);
    if (isEnabled() && m_down) {
        background = QColor::fromRgba(m_kind == Kind::Close ? kErrorActive : kPrimaryActive);
    } else if (isEnabled() && m_hover) {
        background = QColor::fromRgba(m_kind == Kind::Close ? kError : kPrimary);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawEllipse(QRectF(rect()));

    const QRectF glyph((width() - kIconSize) / 2.0, (height() - kIconSize) / 2.0,
                       kIconSize, kIconSize);
    paintGlyph(p, glyph, Qt::white);
}

// 用路径画描边图标，和 snow 的 outlined_icons 视觉一致：
//   编辑 = 铅笔（斜置矩形笔身 + 笔尖）
//   关闭 = 交叉线
void PinControlButton::paintGlyph(QPainter &p, const QRectF &box, const QColor &color)
{
    const qreal scale = box.width() / 18.0;
    p.save();
    p.translate(box.topLeft());
    p.scale(scale, scale);

    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // 所有图形以 (9,9) 为几何中心，外接矩形 3~15，视觉上水平垂直都居中。
    if (m_kind == Kind::Edit) {
        // 编辑 = 方框 + 笔（snow 的 outlined Edit / 通用 PencilSquare）
        // 方框三边开口，笔从右上斜插进去
        QPainterPath frame;
        frame.moveTo(11.6, 3.4);
        frame.lineTo(3.4, 3.4);
        frame.lineTo(3.4, 14.6);
        frame.lineTo(14.6, 14.6);
        frame.lineTo(14.6, 6.4);
        p.drawPath(frame);

        // 笔身：从右下笔尖到左上笔尾，斜跨方框右下角
        QPainterPath pencil;
        pencil.moveTo(6.2, 11.8);
        pencil.lineTo(13.4, 4.6);
        pencil.lineTo(15.0, 6.2);
        pencil.lineTo(7.8, 13.4);
        pencil.closeSubpath();
        p.drawPath(pencil);
    } else {
        // 关闭：以 9,9 为中心的交叉线
        p.drawLine(QPointF(4.6, 4.6), QPointF(13.4, 13.4));
        p.drawLine(QPointF(13.4, 4.6), QPointF(4.6, 13.4));
    }

    p.restore();
}

void PinControlButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_down = true;
        update();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void PinControlButton::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_down) {
        m_down = false;
        update();
        // 松手时还在按钮内才算点击，和 snow 的 AdButton 行为一致
        if (rect().contains(e->position().toPoint())) {
            emit clicked();
            e->accept();
            return;
        }
    }
    QWidget::mouseReleaseEvent(e);
}

void PinControlButton::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QWidget::enterEvent(e);
}

void PinControlButton::leaveEvent(QEvent *e)
{
    m_hover = false;
    m_down = false;
    update();
    QWidget::leaveEvent(e);
}
