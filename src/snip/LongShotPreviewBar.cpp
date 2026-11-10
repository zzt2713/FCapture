#include "snip/LongShotPreviewBar.h"

#include <QMouseEvent>
#include <QPainter>

namespace {
// 对齐 Snow Shot 的视觉参数
constexpr QRgb kBg          = 0x000000;
constexpr QRgb kDimOverlay  = 0x84000000;   // QColor(0,0,0,132) 整体压暗
constexpr QRgb kTrimMask    = 0xB2000000;   // QColor(0,0,0,178) 裁掉区
constexpr QRgb kHoverBand   = 0x401677FF;   // QColor(22,119,255,64) 悬停亮带
constexpr QRgb kHandleColor = 0xFFAD14;     // #faad14 琥珀色手柄
} // namespace

LongShotPreviewBar::LongShotPreviewBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    hide();
}

void LongShotPreviewBar::setImage(const QImage &canvas)
{
    if (canvas.isNull()) {
        clearImage();
        return;
    }
    m_canvas = canvas;
    update();
}

void LongShotPreviewBar::clearImage()
{
    m_canvas = QImage();
    clearHover();
    m_trimTop = 0;
    m_trimBottom = 0;
    m_highlightTop = 0;
    m_highlightBottom = 0;
    update();
}

void LongShotPreviewBar::clearHover()
{
    if (m_hoverActive) {
        m_hoverActive = false;
        m_hoverCanvasRect = QRect();
        emit hoverRegionChanged(QRect());
    }
}

void LongShotPreviewBar::positionAt(const QRect &selectionLocalRect)
{
    if (!parentWidget())
        return;

    const QRect avail = parentWidget()->rect();
    constexpr int kGap = 8;

    int x = selectionLocalRect.right() + kGap;
    if (x + kBarWidth > avail.right()) {
        const int leftX = selectionLocalRect.left() - kGap - kBarWidth;
        if (leftX >= avail.left())
            x = leftX;
        else
            x = qBound(avail.left(), avail.right() - kBarWidth - 4, avail.right());
    }

    const int h = qMax(120, selectionLocalRect.height());
    int y = selectionLocalRect.top();
    if (y + h > avail.bottom())
        y = qMax(avail.top() + 4, avail.bottom() - h - 4);

    setGeometry(x, y, kBarWidth, h);
    show();
    raise();
}

QRect LongShotPreviewBar::imageRect() const
{
    // 整张长图等比缩放进栏内（留出边距 + 两侧手柄余量）
    if (m_canvas.isNull())
        return {};
    const QRect content = rect().adjusted(kPadding, kPadding, -kPadding, -kPadding);
    const qreal scale = qMin(qreal(content.width()) / m_canvas.width(),
                            qreal(content.height()) / m_canvas.height());
    const int w = qMax(1, qRound(m_canvas.width() * scale));
    const int h = qMax(1, qRound(m_canvas.height() * scale));
    return QRect(content.left() + (content.width() - w) / 2,
                 content.top() + (content.height() - h) / 2, w, h);
}

qreal LongShotPreviewBar::imageScale() const
{
    const QRect r = imageRect();
    return (m_canvas.isNull() || m_canvas.height() == 0)
               ? 1.0 : qreal(r.height()) / m_canvas.height();
}

int LongShotPreviewBar::yToImage(int widgetY) const
{
    const QRect r = imageRect();
    const qreal scale = imageScale();
    return qBound(0, qRound((widgetY - r.top()) / scale), m_canvas.height());
}

int LongShotPreviewBar::imageToY(int imageY) const
{
    const QRect r = imageRect();
    return r.top() + qRound(imageY * imageScale());
}

// 悬停亮带 = 捕获视口大小的矩形，中心跟随光标并钳制在画布内
//（Snow Shot updateHover() 同款映射）
QRect LongShotPreviewBar::hoverCanvasRect(const QPoint &pos) const
{
    if (m_canvas.isNull())
        return {};
    const QRect img = imageRect();
    const qreal scale = imageScale();
    const int cx = qRound((pos.x() - img.left()) / scale);
    const int cy = qRound((pos.y() - img.top()) / scale);
    const int w = qMin(m_viewportSize.width(), m_canvas.width());
    const int h = qMin(m_viewportSize.height(), m_canvas.height());
    const int left = qBound(0, cx - w / 2, m_canvas.width() - w);
    const int top = qBound(0, cy - h / 2, m_canvas.height() - h);
    return QRect(left, top, w, h);
}

LongShotPreviewBar::Hit LongShotPreviewBar::hitAt(const QPoint &pos) const
{
    if (m_canvas.isNull())
        return Hit::None;

    const QRect r = imageRect();
    const int headY = imageToY(m_trimTop);
    const int tailY = imageToY(m_canvas.height() - m_trimBottom);

    // 手柄命中只认捏手附近（Snow Shot 的 kHandleHitRadius 围着 tab）：
    // 整条横线都算命中的时候，正文区顶/底边缘的普通点击会被误判成裁剪拖拽，
    // 一抖就把长图头/尾裁掉了
    const int tabHalf = kHandleTabExtent / 2 + 16;
    const bool nearTab = qAbs(pos.x() - r.center().x()) <= tabHalf;
    if (nearTab && qAbs(pos.y() - headY) <= kHandleHitRadius)
        return Hit::Head;
    if (nearTab && qAbs(pos.y() - tailY) <= kHandleHitRadius)
        return Hit::Tail;
    return r.contains(pos) ? Hit::Body : Hit::None;
}

void LongShotPreviewBar::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 背板
    p.setPen(Qt::NoPen);
    p.setBrush(QColor::fromRgba(0xEB000000 | kBg));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    if (m_canvas.isNull()) {
        p.setPen(QColor(0x8A, 0x8A, 0x8A));
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        p.drawText(rect().adjusted(4, 4, -4, -4), Qt::AlignCenter, QStringLiteral("尚无内容"));
        return;
    }

    const QRect img = imageRect();

    // 整图 + 整体压暗（Snow Shot 同款层次）
    p.drawImage(img, m_canvas);
    p.fillRect(img, QColor::fromRgba(kDimOverlay));

    // 最近捕获段画亮（Snow Shot 的 m_highlightedRows 同款：盖过压暗层重画）
    if (m_highlightBottom > m_highlightTop) {
        const QRect band(img.left(), imageToY(m_highlightTop), img.width(),
                         qRound((m_highlightBottom - m_highlightTop) * imageScale()));
        const QRect clipped = band.intersected(img);
        if (!clipped.isEmpty()) {
            p.save();
            p.setClipRect(clipped);
            p.drawImage(img, m_canvas);
            p.restore();
        }
    }

    // 裁掉的头/尾压得更暗
    if (m_trimTop > 0) {
        const int h = qRound(m_trimTop * imageScale());
        p.fillRect(QRect(img.left(), img.top(), img.width(), qMin(h, img.height())),
                   QColor::fromRgba(kTrimMask));
    }
    if (m_trimBottom > 0) {
        const int h = qRound(m_trimBottom * imageScale());
        p.fillRect(QRect(img.left(), img.bottom() - h + 1, img.width(), qMin(h, img.height())),
                   QColor::fromRgba(kTrimMask));
    }

    // 悬停亮带：视口大小的矩形，指示"选区里正在预览的范围"
    if (m_hoverActive && m_dragging == Hit::None && !m_hoverCanvasRect.isEmpty()) {
        const qreal scale = imageScale();
        QRectF band(img.left() + m_hoverCanvasRect.left() * scale,
                    img.top() + m_hoverCanvasRect.top() * scale,
                    m_hoverCanvasRect.width() * scale,
                    m_hoverCanvasRect.height() * scale);
        band = band.intersected(QRectF(img));
        if (!band.isEmpty())
            p.fillRect(band, QColor::fromRgba(kHoverBand));
    }

    // 头/尾手柄：横线 + 居中圆角捏手（照抄 drawTrimHandle）
    const auto drawHandle = [&](int imageY) {
        const int y = imageToY(imageY);
        if (y < img.top() - kHandleHitRadius || y > img.bottom() + kHandleHitRadius)
            return;
        const int lineY = qBound(img.top(), y, img.bottom());
        p.fillRect(QRect(img.left(), lineY - kHandleStrokeWidth / 2,
                         img.width(), kHandleStrokeWidth),
                   QColor::fromRgba(0xFF000000 | kHandleColor));
        const int tabW = qMin(kHandleTabExtent, img.width());
        p.drawRoundedRect(QRectF(img.center().x() - tabW / 2.0,
                                 lineY - kHandleThickness / 2.0,
                                 tabW, kHandleThickness),
                          2.0, 2.0);
    };
    drawHandle(m_trimTop);
    drawHandle(m_canvas.height() - m_trimBottom);
}

void LongShotPreviewBar::wheelEvent(QWheelEvent *e)
{
    // 缩略图等比放得下整图时滚轮没用；保持事件不外泄即可
    e->accept();
}

void LongShotPreviewBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_canvas.isNull()) {
        QWidget::mousePressEvent(e);
        return;
    }
    const Hit hit = hitAt(e->position().toPoint());
    if (hit == Hit::Head || hit == Hit::Tail) {
        m_dragging = hit;
        grabMouse();
        // 拖裁剪期间不预览（亮带跟着裁剪边走的复杂交互从略）
        clearHover();
    }
}

void LongShotPreviewBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_canvas.isNull())
        return;

    const QPoint pos = e->position().toPoint();

    // 拖动手柄：改裁剪范围
    if (m_dragging != Hit::None) {
        const int imageY = yToImage(pos.y());
        const int minKeep = qMin(kMinTrimmed, m_canvas.height());
        if (m_dragging == Hit::Head) {
            m_trimTop = qBound(0, imageY, m_canvas.height() - m_trimBottom - minKeep);
        } else {
            m_trimBottom = qBound(0, m_canvas.height() - imageY, m_canvas.height() - m_trimTop - minKeep);
        }
        emit trimChanged(m_trimTop, m_trimBottom);
        update();
        return;
    }

    // 悬停：视口亮带跟随 + 持续上报对应画布区段（悬停即预览）
    const Hit hit = hitAt(pos);
    setCursor(hit == Hit::Head || hit == Hit::Tail ? Qt::SizeVerCursor
              : hit == Hit::Body ? Qt::PointingHandCursor
                                : Qt::ArrowCursor);
    const QRect rect = hoverCanvasRect(pos);
    if (!m_hoverActive || rect != m_hoverCanvasRect) {
        m_hoverActive = true;
        m_hoverCanvasRect = rect;
        emit hoverRegionChanged(rect);
        update();
    }
}

void LongShotPreviewBar::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (m_dragging != Hit::None) {
        m_dragging = Hit::None;
        releaseMouse();
        update();
    }
}

void LongShotPreviewBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    // 双击手柄 = 复位裁剪
    if (!m_canvas.isNull()) {
        const Hit hit = hitAt(e->position().toPoint());
        if (hit == Hit::Head && m_trimTop != 0) {
            m_trimTop = 0;
            emit trimChanged(m_trimTop, m_trimBottom);
            update();
        } else if (hit == Hit::Tail && m_trimBottom != 0) {
            m_trimBottom = 0;
            emit trimChanged(m_trimTop, m_trimBottom);
            update();
        }
    }
    QWidget::mouseDoubleClickEvent(e);
}

void LongShotPreviewBar::leaveEvent(QEvent *e)
{
    Q_UNUSED(e);
    clearHover();
    update();
}
