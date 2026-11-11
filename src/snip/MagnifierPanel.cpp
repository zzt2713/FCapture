#include "snip/MagnifierPanel.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace {
// ---- 逐项对齐 Snow Shot 的 ScreenshotColorPickerWindow ----
constexpr int kPreviewScale = 12;                    // 每格 12px
constexpr int kPreviewPickerSize = 11;               // 11×11 像素
constexpr int kPreviewCanvasSize = kPreviewScale * kPreviewPickerSize;   // 132
constexpr int kPanelPadding = 4;
constexpr int kPanelRadius = 6;
constexpr int kContentGap = 4;
constexpr int kTextVerticalOffset = 1;
constexpr int kTextHeight = 20;
constexpr int kTextPixelSize = 13;
constexpr int kColorTextHeight = 24;
constexpr int kCenterPixelBorderWidth = 1;
constexpr int kShadowMargin = 10;
constexpr int kShadowBlurRadius = 8;
constexpr int kCursorGap = 16;

const QColor kShadowColor(0, 0, 0, 28);

// 色块上的文字用反差色：浅色块配深字、深色块配浅字
QColor readableTextColor(const QColor &color)
{
    if (!color.isValid())
        return QColor(255, 255, 255, 217);
    return (color.red() > 128 || color.green() > 128 || color.blue() > 128)
               ? QColor(0, 0, 0, 224)
               : QColor(255, 255, 255, 217);
}

QString fitText(const QFont &font, const QString &text, int width)
{
    QFontMetrics metrics(font);
    if (metrics.horizontalAdvance(text) <= width)
        return text;
    return metrics.elidedText(text, Qt::ElideRight, width);
}

// 上方两个角圆角、下方直角（网格贴合面板顶部）
QPainterPath topRoundedRectPath(const QRectF &rect, qreal radius)
{
    const qreal r = std::min<qreal>(radius, std::min(rect.width(), rect.height()) / 2.0);
    QPainterPath path;
    path.moveTo(rect.left(), rect.top() + r);
    path.quadTo(rect.topLeft(), QPointF(rect.left() + r, rect.top()));
    path.lineTo(rect.right() - r, rect.top());
    path.quadTo(rect.topRight(), QPointF(rect.right(), rect.top() + r));
    path.lineTo(rect.bottomRight());
    path.lineTo(rect.bottomLeft());
    path.lineTo(rect.left(), rect.top() + r);
    path.closeSubpath();
    return path;
}

// 下方两个角圆角、上方直角（色块贴合面板底部）
QPainterPath bottomRoundedRectPath(const QRectF &rect, qreal radius)
{
    const qreal r = std::min<qreal>(radius, std::min(rect.width(), rect.height()) / 2.0);
    QPainterPath path;
    path.moveTo(rect.topLeft());
    path.lineTo(rect.topRight());
    path.lineTo(rect.right(), rect.bottom() - r);
    path.quadTo(rect.bottomRight(), QPointF(rect.right() - r, rect.bottom()));
    path.lineTo(rect.left() + r, rect.bottom());
    path.quadTo(rect.bottomLeft(), QPointF(rect.left(), rect.bottom() - r));
    path.lineTo(rect.topLeft());
    path.closeSubpath();
    return path;
}
} // namespace

MagnifierPanel::MagnifierPanel()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput
                          | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);

    // 尺寸固定（Snow 的 sizeHint）：网格 + 坐标行 + 色块行
    const int panelW = kPanelPadding * 2 + kPreviewCanvasSize;
    const int panelH = kPanelPadding + kPreviewCanvasSize + kContentGap + kTextVerticalOffset
                       + kTextHeight + kContentGap + kTextVerticalOffset + kColorTextHeight
                       + kPanelPadding;
    setFixedSize(panelW + kShadowMargin * 2, panelH + kShadowMargin * 2);
    hide();
}

void MagnifierPanel::showPanel(const Data &sample, const QPoint &anchorGlobal,
                               const QRect &avoidGlobal)
{
    m_data = sample;

    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen ? screen->geometry() : QRect(0, 0, 1920, 1080);

    // 光标右下 gap=16，越界翻到左/上，最后钳制在屏内（Snow 的 cursorPanelPosition）
    const QPoint bottomRight = anchorGlobal + QPoint(kCursorGap, kCursorGap);
    const bool useLeft = bottomRight.x() + width() > bounds.left() + bounds.width();
    const bool useTop = bottomRight.y() + height() > bounds.top() + bounds.height();
    QPoint pos(useLeft ? anchorGlobal.x() - kCursorGap - width() : bottomRight.x(),
               useTop ? anchorGlobal.y() - kCursorGap - height() : bottomRight.y());

    // 避开工具栏
    if (!avoidGlobal.isEmpty() && QRect(pos, size()).intersects(avoidGlobal)) {
        const QPoint leftPos(anchorGlobal.x() - kCursorGap - width(), pos.y());
        const QPoint topPos(pos.x(), anchorGlobal.y() - kCursorGap - height());
        if (!QRect(leftPos, size()).intersects(avoidGlobal))
            pos = leftPos;
        else if (!QRect(topPos, size()).intersects(avoidGlobal))
            pos = topPos;
    }

    pos.setX(qBound(bounds.left(), pos.x(),
                    qMax(bounds.left(), bounds.left() + bounds.width() - width())));
    pos.setY(qBound(bounds.top(), pos.y(),
                    qMax(bounds.top(), bounds.top() + bounds.height() - height())));
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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel(kShadowMargin + 0.5, kShadowMargin + 0.5,
                       width() - kShadowMargin * 2, height() - kShadowMargin * 2);

    // 手绘多层阴影（Snow 同款：8 层、rgba(0,0,0,28)、alpha 按 pow 渐变）
    for (int step = kShadowBlurRadius; step >= 1; --step) {
        const qreal progress = qreal(kShadowBlurRadius - step + 1) / kShadowBlurRadius;
        QColor shadow = kShadowColor;
        shadow.setAlphaF(std::clamp<qreal>(
            qreal(kShadowColor.alpha()) / 255.0 * std::pow(progress, 1.45), 0.0, 1.0));
        painter.setPen(QPen(shadow, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(panel.adjusted(-step, -step, step, step),
                                kPanelRadius + step, kPanelRadius + step);
    }

    QPainterPath panelPath;
    panelPath.addRoundedRect(panel, kPanelRadius, kPanelRadius);
    painter.fillPath(panelPath, Theme::kToolbarBg);

    // ---- 1. 像素网格（顶部，上圆角下直角）----
    const QRectF preview(panel.left() + kPanelPadding, panel.top() + kPanelPadding,
                         kPreviewCanvasSize, kPreviewCanvasSize);
    painter.save();
    painter.setClipPath(topRoundedRectPath(preview, kPanelRadius));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (!m_data.sample.isNull())
        painter.drawImage(preview, m_data.sample);
    else
        painter.fillRect(preview, Qt::black);
    painter.restore();

    // 中心像素标记 + 十字参考线（Snow 的 paintScreenshotColorPickerCenterGuideLines）
    constexpr int centerOffset = (kPreviewPickerSize / 2) * kPreviewScale;
    const QRectF centerRect(preview.left() + centerOffset, preview.top() + centerOffset,
                            kPreviewScale, kPreviewScale);
    if (m_data.color.isValid()) {
        painter.setPen(QPen(Theme::kGuideLine, 1));
        painter.drawLine(QPointF(preview.center().x(), preview.top()),
                         QPointF(preview.center().x(), preview.bottom()));
        painter.drawLine(QPointF(preview.left(), preview.center().y()),
                         QPointF(preview.right(), preview.center().y()));

        painter.setPen(QPen(readableTextColor(m_data.color), kCenterPixelBorderWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(centerRect.adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);
    }

    // ---- 2. 坐标文本（居中）----
    QFont textFont = font();
    textFont.setPixelSize(kTextPixelSize);
    painter.setFont(textFont);
    painter.setPen(Theme::kText);

    const QRectF positionRect(preview.left(), preview.bottom() + kContentGap + kTextVerticalOffset,
                              kPreviewCanvasSize, kTextHeight);
    const QString positionText = QStringLiteral("X: %1  Y: %2")
                                     .arg(m_data.position.x())
                                     .arg(m_data.position.y());
    painter.drawText(positionRect, Qt::AlignCenter,
                     fitText(textFont, positionText, int(positionRect.width())));

    // ---- 3. 色块行（整行填色，文字反差色居中，下圆角）----
    const QRectF colorRect(positionRect.left(),
                           positionRect.bottom() + kContentGap + kTextVerticalOffset,
                           kPreviewCanvasSize, kColorTextHeight);
    const QColor swatch = m_data.color.isValid() ? m_data.color : QColor(0, 0, 0);
    painter.setPen(Qt::NoPen);
    painter.fillPath(bottomRoundedRectPath(colorRect, kPanelRadius), swatch);

    const QString colorText = !m_data.color.isValid()
                                  ? QString()
                                  : (m_data.hexFormat
                                         ? m_data.color.name(QColor::HexRgb).toUpper()
                                         : QStringLiteral("RGB %1, %2, %3")
                                               .arg(m_data.color.red())
                                               .arg(m_data.color.green())
                                               .arg(m_data.color.blue()));
    painter.setPen(readableTextColor(swatch));
    painter.drawText(colorRect.adjusted(4.0, 0.0, -4.0, 0.0), Qt::AlignCenter,
                     fitText(textFont, colorText, int(colorRect.width()) - 8));
}
