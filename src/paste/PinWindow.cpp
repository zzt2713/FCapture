#include "paste/PinWindow.h"

#include "core/ImageOutput.h"
#include "paste/PinManager.h"
#include "platform/win/VirtualDesktop.h"
#include "platform/win/WinUtil.h"
#include "ui/OperationToast.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

namespace {

constexpr qreal kMinScale = 0.05;
constexpr qreal kMaxScale = 20.0;
constexpr qreal kMinOpacity = 0.05;
constexpr int kShadowMargin = 8;
constexpr int kShadowLayers = 6;

int chessCell() { return 8; }

} // namespace

PinWindow::PinWindow(const QImage &image, const QString &sourceText,
                     qreal imageDpr, QWidget *parent)
    : QWidget(parent)
    , m_original(image)
    , m_sourceText(sourceText)
    , m_imageDpr(imageDpr > 0.0 ? imageDpr : 1.0)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    setCursor(Qt::OpenHandCursor);

    applyTopmost();
    rebuildDisplay();
    updateWindowSize();

    m_scaleHudTimer = new QTimer(this);
    m_scaleHudTimer->setSingleShot(true);
    m_scaleHudTimer->setInterval(1000);
    connect(m_scaleHudTimer, &QTimer::timeout, this, [this]() {
        m_scaleHudVisible = false;
        update();
    });

    // 应用级焦点变化：点任何其它程序/窗口都会触发，
    // 比单靠窗口自己的 ActivationChange 可靠。
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget * /*old*/, QWidget *now) {
                const bool active = (now == this);
                if (active != m_active) {
                    m_active = active;
                    update();
                }
            });
}

PinWindow::~PinWindow() = default;

// 虚拟桌面：窗口只在创建它的桌面上可见。
// 由 AppController 在桌面切换时统一查询，不在这里主动 show/hide，
// 避免和用户的隐藏/显示操作打架。
bool PinWindow::isOnCurrentDesktop() const
{
    if (m_desktopId.isNull() || !VirtualDesktop::isSupported())
        return true;
    return VirtualDesktop::isWindowOnCurrentDesktop(winId());
}

QRect PinWindow::contentRect() const
{
    return rect().adjusted(kShadowMargin, kShadowMargin,
                           -kShadowMargin, -kShadowMargin);
}

// ---------------------------------------------------------------- 变换

QImage PinWindow::transformed(const QImage &src) const
{
    QImage img = src;

    // 旋转
    switch (m_rotation) {
    case 90:  img = img.transformed(QTransform().rotate(90));  break;
    case 180: img = img.transformed(QTransform().rotate(180)); break;
    case 270: img = img.transformed(QTransform().rotate(270)); break;
    default: break;
    }

    // 镜像
    if (m_flipH || m_flipV)
        img = img.mirrored(m_flipH, m_flipV);

    // 灰度
    if (m_grayscale) {
        img = img.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb px = line[x];
                const int gray = qGray(px);
                line[x] = qRgba(gray, gray, gray, qAlpha(px));
            }
        }
    }

    // 反色（保留 alpha）
    if (m_inverted)
        img.invertPixels(QImage::InvertRgba);

    return img;
}

void PinWindow::rebuildDisplay()
{
    m_display = transformed(m_original);
    m_pixmap = QPixmap::fromImage(m_display);
    updateWindowSize();
    update();
}

// 100% 缩放时窗口应有的逻辑尺寸。
// 图像可能是物理像素（截图），需要除以源图像 DPR 才是屏幕上的逻辑尺寸。
QSize PinWindow::baseLogicalSize() const
{
    if (m_display.isNull())
        return {};

    const qreal dpr = (m_imageDpr > 0.0) ? m_imageDpr : 1.0;
    return QSize(qRound(m_display.width() / dpr),
                 qRound(m_display.height() / dpr));
}

QSize PinWindow::scaledContentSize() const
{
    if (m_display.isNull())
        return {};

    const QSize base = baseLogicalSize();
    return QSize(qRound(base.width() * m_scale),
                 qRound(base.height() * m_scale)).expandedTo(QSize(16, 16));
}

void PinWindow::updateWindowSize()
{
    const QSize content = scaledContentSize();
    if (content.isEmpty())
        return;
    resize(content + QSize(kShadowMargin * 2, kShadowMargin * 2));
}

QRect PinWindow::imageTargetRect() const
{
    const QRect content = contentRect();
    const QSize target = baseLogicalSize().scaled(content.size(), Qt::KeepAspectRatio);
    return QRect(content.topLeft()
                     + QPoint((content.width() - target.width()) / 2,
                              (content.height() - target.height()) / 2),
                 target);
}

// ---------------------------------------------------------------- 操作

void PinWindow::replaceImage(const QImage &img)
{
    if (img.isNull())
        return;
    m_original = img;
    rebuildDisplay();
}

void PinWindow::zoomBy(qreal factor)
{
    const qreal next = qBound(kMinScale, m_scale * factor, kMaxScale);
    if (qFuzzyCompare(next, m_scale))
        return;

    const QPoint topLeft = pos();
    m_scale = next;
    setGeometry(QRect(topLeft, scaledContentSize()
                                  + QSize(kShadowMargin * 2, kShadowMargin * 2)));
    showScaleHud();
    update();
}

void PinWindow::setScale(qreal s)
{
    const qreal next = qBound(kMinScale, s, kMaxScale);
    if (qFuzzyCompare(next, m_scale))
        return;

    const QPoint topLeft = pos();
    m_scale = next;
    setGeometry(QRect(topLeft, scaledContentSize()
                                  + QSize(kShadowMargin * 2, kShadowMargin * 2)));
    showScaleHud();
    update();
}

void PinWindow::rotateCW()
{
    m_rotation = (m_rotation + 90) % 360;
    rebuildDisplay();
}

void PinWindow::rotateCCW()
{
    m_rotation = (m_rotation + 270) % 360;
    rebuildDisplay();
}

void PinWindow::flipHorizontal()
{
    m_flipH = !m_flipH;
    rebuildDisplay();
}

void PinWindow::flipVertical()
{
    m_flipV = !m_flipV;
    rebuildDisplay();
}

void PinWindow::toggleGrayscale()
{
    m_grayscale = !m_grayscale;
    rebuildDisplay();
}

void PinWindow::toggleInverted()
{
    m_inverted = !m_inverted;
    rebuildDisplay();
}

void PinWindow::resetTransform()
{
    m_scale = 1.0;
    m_opacity = 1.0;
    m_rotation = 0;
    m_flipH = false;
    m_flipV = false;
    m_grayscale = false;
    m_inverted = false;
    rebuildDisplay();
    setWindowOpacity(1.0);
    showScaleHud();
}

void PinWindow::setOpacityValue(qreal o)
{
    m_opacity = qBound(kMinOpacity, o, 1.0);
    // 用窗口透明度，图片本身不受影响（导出时仍是原色）
    setWindowOpacity(m_opacity);
    update();
}

void PinWindow::setSelected(bool sel)
{
    if (m_selected == sel)
        return;
    m_selected = sel;
    update();
}

void PinWindow::setClickThrough(bool on)
{
    if (m_clickThrough == on)
        return;
    m_clickThrough = on;
    setWindowFlag(Qt::WindowTransparentForInput, on);
    show();   // 窗口标志变更后需要重新显示
    update();
}

void PinWindow::applyTopmost()
{
    setWindowFlag(Qt::WindowStaysOnTopHint, m_topmost);
}

// 显示窗口时检查虚拟桌面归属：不在当前桌面就不显示
void PinWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!isOnCurrentDesktop())
        hide();
}

void PinWindow::setTopmost(bool on)
{
    if (m_topmost == on)
        return;
    m_topmost = on;
    applyTopmost();
    show();
}

void PinWindow::setBackgroundMode(BackgroundMode mode)
{
    m_bgMode = mode;
    update();
}

// ---------------------------------------------------------------- 四角缩放

PinWindow::Corner PinWindow::cornerAt(const QPoint &pos) const
{
    const QRect content = contentRect();
    const int t = 10;   // 角区命中范围
    const bool left = pos.x() >= content.left() && pos.x() <= content.left() + t;
    const bool right = pos.x() >= content.right() - t && pos.x() <= content.right();
    const bool top = pos.y() >= content.top() && pos.y() <= content.top() + t;
    const bool bottom = pos.y() >= content.bottom() - t && pos.y() <= content.bottom();

    if (left && top) return Corner::TopLeft;
    if (right && top) return Corner::TopRight;
    if (left && bottom) return Corner::BottomLeft;
    if (right && bottom) return Corner::BottomRight;
    return Corner::None;
}

void PinWindow::beginResize(Corner c, const QPoint &globalPos)
{
    m_resizeCorner = c;
    m_resizing = true;
    m_resizeStartGlobal = globalPos;
    m_resizeStartContentGeo =
        QRect(geometry().topLeft() + contentRect().topLeft(), contentRect().size());
    m_resizeStartScale = m_scale;
}

void PinWindow::updateResize(const QPoint &globalPos)
{
    if (!m_resizing || m_resizeCorner == Corner::None)
        return;

    const QPoint delta = globalPos - m_resizeStartGlobal;
    const QSize base = baseLogicalSize();
    if (base.isEmpty())
        return;

    // 按水平位移算缩放比例（四角等比缩放）
    const int horizontalSign = (m_resizeCorner == Corner::TopLeft
                                || m_resizeCorner == Corner::BottomLeft) ? -1 : 1;

    const qreal startW = qMax(1, m_resizeStartContentGeo.width());
    const qreal newW = startW + delta.x() * horizontalSign;
    qreal nextScale = m_resizeStartScale * (newW / startW);
    nextScale = qBound(kMinScale, nextScale, kMaxScale);

    m_scale = nextScale;
    const QSize newSize = scaledContentSize();

    // 左侧/上方角：内容左上角跟着动，保证对角固定。
    QRect g = m_resizeStartContentGeo;
    QPoint contentTopLeft = g.topLeft();

    if (m_resizeCorner == Corner::TopLeft || m_resizeCorner == Corner::BottomLeft)
        contentTopLeft.setX(g.right() - newSize.width() + 1);
    if (m_resizeCorner == Corner::TopLeft || m_resizeCorner == Corner::TopRight)
        contentTopLeft.setY(g.bottom() - newSize.height() + 1);

    const QPoint windowTopLeft =
        contentTopLeft - QPoint(kShadowMargin, kShadowMargin);
    setGeometry(QRect(windowTopLeft,
                      newSize + QSize(kShadowMargin * 2, kShadowMargin * 2)));
    showScaleHud();
    update();
}

// ---------------------------------------------------------------- 裁剪

void PinWindow::enterCropMode()
{
    m_cropping = true;
    m_cropRect = QRect();
    setCursor(Qt::CrossCursor);
    update();
}

void PinWindow::applyCrop()
{
    if (m_cropRect.isEmpty() || m_display.isNull())
        return;

    const QRect content = contentRect();
    const QRect imageRect = imageTargetRect();
    const QRect displayRect = m_cropRect.intersected(content).intersected(imageRect);
    if (displayRect.width() < 4 || displayRect.height() < 4)
        return;

    // 显示坐标 → 原图坐标
    const QPoint localTopLeft = displayRect.topLeft() - imageRect.topLeft();
    const qreal sx = qreal(m_display.width()) / imageRect.width();
    const qreal sy = qreal(m_display.height()) / imageRect.height();

    const QRect srcRect(qRound(localTopLeft.x() * sx), qRound(localTopLeft.y() * sy),
                        qRound(displayRect.width() * sx), qRound(displayRect.height() * sy));

    const QImage cropped = m_display.copy(srcRect.intersected(m_display.rect()));
    if (!cropped.isNull()) {
        // 裁剪后重置变换，把裁剪结果作为新的原图
        m_original = cropped;
        m_rotation = 0;
        m_flipH = false;
        m_flipV = false;
        m_grayscale = false;
        m_inverted = false;
        m_scale = 1.0;
        rebuildDisplay();
    }

    m_cropping = false;
    m_cropRect = QRect();
    setCursor(Qt::OpenHandCursor);
    update();
}

void PinWindow::cancelCrop()
{
    m_cropping = false;
    m_cropRect = QRect();
    setCursor(Qt::OpenHandCursor);
    update();
}

QPixmap PinWindow::snapshotForRecovery() const
{
    return QPixmap::fromImage(m_display);
}

// ---------------------------------------------------------------- 绘制

void PinWindow::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    const bool active = m_active || isActiveWindow() || m_dragging || m_resizing;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    drawShadow(p, active);
    drawBackground(p);

    if (!m_pixmap.isNull())
        p.drawPixmap(imageTargetRect(), m_pixmap);

    if (m_cropping)
        drawCropOverlay(p);

    drawScaleHud(p);

    if (active && !m_cropping) {
        drawGlow(p);
        drawResizeHandles(p);
    }
}

void PinWindow::showScaleHud()
{
    m_scaleHudVisible = true;
    if (m_scaleHudTimer)
        m_scaleHudTimer->start();
}

void PinWindow::drawScaleHud(QPainter &p)
{
    // 缩放进行中（拖角/滚轮）时一直显示；松手后再由定时器隐藏。
    if (!m_scaleHudVisible && !m_resizing)
        return;

    // 角标贴在图片本身的左上角，而不是窗口内容区左上角：
    // 内容区含 8px 阴影边距，图片放大后会先被阴影区"推"得偏离，看起来像没显示。
    const QRect anchor = imageTargetRect().isValid() ? imageTargetRect() : contentRect();

    QFont hudFont = font();
    hudFont.setPointSize(9);
    const QFontMetrics fm(hudFont);
    const QString text = QStringLiteral("%1%").arg(qRound(m_scale * 100.0));

    // 宽高下限写死：小图（缩略图 40px）时按 content.width()-16 算会得到负值，
    // 整块角标直接被丢掉，用户反馈"缩放时看不到比例"就是这个原因。
    const int labelWidth = qBound(44, fm.horizontalAdvance(text) + 16, 220);
    const int labelHeight = qBound(20, fm.height() + 8, 40);

    // 文字放不下时（极小的缩略图）只画百分比数字，贴满图片宽度
    QRect label(anchor.left(), anchor.top(), labelWidth, labelHeight);
    label.setWidth(qMin(labelWidth, qMax(24, anchor.width())));
    label.setHeight(qMin(labelHeight, qMax(18, anchor.height())));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(label, 4, 4);
    p.setFont(hudFont);
    p.setPen(QColor(0xF2, 0xF2, 0xF2));
    p.drawText(label, Qt::AlignCenter, text);
    p.restore();
}

void PinWindow::drawShadow(QPainter &p, bool active)
{
    p.save();
    p.setPen(Qt::NoPen);

    const QColor shadow = active ? QColor(0x3D, 0x8B, 0xFD) : QColor(0, 0, 0);
    const QRectF content(contentRect());
    for (int i = kShadowLayers; i >= 1; --i) {
        QColor color = shadow;
        color.setAlpha(active ? 3 + (kShadowLayers - i) * 2
                              : 5 + (kShadowLayers - i) * 2);
        p.setBrush(color);
        const qreal spread = i;
        p.drawRoundedRect(content.adjusted(-spread, -spread, spread, spread),
                          spread * 0.75, spread * 0.75);
    }

    p.restore();
}

void PinWindow::drawGlow(QPainter &p)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::kAccent, 1.2));
    p.drawRect(QRectF(contentRect()).adjusted(0.6, 0.6, -0.6, -0.6));

    p.restore();
}

void PinWindow::drawResizeHandles(QPainter &p)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::kAccent);

    const QRect content = contentRect();
    const int s = 6;
    const QPoint pts[4] = {
        content.topLeft() + QPoint(1, 1),
        QPoint(content.right() - s, content.top() + 1),
        QPoint(content.left() + 1, content.bottom() - s),
        QPoint(content.right() - s, content.bottom() - s)
    };
    for (const QPoint &pt : pts)
        p.drawRect(QRect(pt, QSize(s, s)));

    p.restore();
}

void PinWindow::drawBackground(QPainter &p)
{
    const QRect content = contentRect();

    switch (m_bgMode) {
    case BackgroundMode::Transparent:
    case BackgroundMode::PseudoTransparent:
        return;   // 什么都不画，透明

    case BackgroundMode::DarkChessboard: {
        const int c = chessCell();
        p.save();
        p.setClipRect(content);
        p.fillRect(content, QColor(0x40, 0x40, 0x40));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x30, 0x30, 0x30));
        for (int y = content.top(); y <= content.bottom(); y += c) {
            for (int x = content.left(); x <= content.right(); x += c) {
                if ((((x - content.left()) / c) + ((y - content.top()) / c)) % 2)
                    p.drawRect(QRect(x, y, c, c));
            }
        }
        p.restore();
        break;
    }
    case BackgroundMode::LightChessboard: {
        const int c = chessCell();
        p.save();
        p.setClipRect(content);
        p.fillRect(content, QColor(0xFF, 0xFF, 0xFF));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xE8, 0xE8, 0xE8));
        for (int y = content.top(); y <= content.bottom(); y += c) {
            for (int x = content.left(); x <= content.right(); x += c) {
                if ((((x - content.left()) / c) + ((y - content.top()) / c)) % 2)
                    p.drawRect(QRect(x, y, c, c));
            }
        }
        p.restore();
        break;
    }
    }
}

void PinWindow::drawCropOverlay(QPainter &p)
{
    p.save();

    const QRect content = contentRect();
    const QRect imageRect = imageTargetRect();

    // 阴影边距不参与截图内容，遮罩只覆盖图片内容区。
    p.fillRect(content, QColor(0, 0, 0, 120));

    if (!m_cropRect.isEmpty()) {
        const QRect r = m_cropRect.intersected(content).intersected(imageRect);
        if (!r.isEmpty() && !m_display.isNull() && imageRect.isValid()) {
            const qreal sx = qreal(m_display.width()) / imageRect.width();
            const qreal sy = qreal(m_display.height()) / imageRect.height();
            const QRectF local(QRectF(r).translated(-imageRect.topLeft()));
            const QRectF source(local.x() * sx, local.y() * sy,
                                local.width() * sx, local.height() * sy);
            p.drawImage(QRectF(r), m_display, source);
        }

        p.setPen(QPen(Theme::kAccent, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);

        // 尺寸标签总是留在图片内容区左上角附近，不覆盖窗口阴影。
        QFont f = font();
        f.setPointSize(8);
        p.setFont(f);
        const QString text = QStringLiteral("%1 × %2").arg(m_cropRect.width())
                                 .arg(m_cropRect.height());
        const QFontMetrics fm(f);
        const int labelWidth = fm.horizontalAdvance(text) + 12;
        const int labelHeight = 18;
        int labelX = r.left();
        if (labelX + labelWidth - 1 > content.right())
            labelX = content.right() - labelWidth + 1;
        labelX = qMax(content.left(), labelX);
        int labelY = r.top() - labelHeight - 4;
        if (labelY < content.top())
            labelY = r.top() + 4;
        labelY = qMax(content.top(), labelY);
        if (labelY + labelHeight - 1 > content.bottom())
            labelY = content.bottom() - labelHeight + 1;
        const QRect label(labelX, labelY, labelWidth, labelHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRoundedRect(label, 3, 3);
        p.setPen(Qt::white);
        p.drawText(label, Qt::AlignCenter, text);
    }

    p.restore();
}

// ---------------------------------------------------------------- 事件

void PinWindow::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();

    if (m_cropping && e->button() == Qt::LeftButton) {
        const QRect imageRect = imageTargetRect();
        if (!imageRect.contains(pos))
            return;
        m_cropStart = pos;
        m_cropRect = QRect(pos, QSize(0, 0));
        update();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        // 四角 → 缩放；其它位置 → 拖动
        const Corner c = cornerAt(pos);
        if (c != Corner::None && !m_cropping) {
            beginResize(c, e->globalPosition().toPoint());
            raise();
            activateWindow();
            return;
        }

        m_dragging = true;
        m_dragStartGlobal = e->globalPosition().toPoint();
        m_dragStartPos = this->pos();
        setCursor(Qt::ClosedHandCursor);
        raise();
        activateWindow();
    }

    emit clicked(this, e->button(), e->modifiers());
    QWidget::mousePressEvent(e);
}

void PinWindow::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();

    if (m_cropping && (e->buttons() & Qt::LeftButton)) {
        m_cropRect = QRect(m_cropStart, pos).normalized().intersected(imageTargetRect());
        update();
        return;
    }

    if (m_resizing && (e->buttons() & Qt::LeftButton)) {
        updateResize(e->globalPosition().toPoint());
        emit moved(this);
        return;
    }

    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        const QPoint delta = e->globalPosition().toPoint() - m_dragStartGlobal;
        move(m_dragStartPos + delta);
        emit dragged(this, delta);
        emit moved(this);
        return;
    }

    // 悬停光标：四角显示斜向缩放，其它位置显示抓手
    if (!m_cropping && !m_dragging && !m_resizing) {
        switch (cornerAt(pos)) {
        case Corner::TopLeft:
        case Corner::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Corner::TopRight:
        case Corner::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Corner::None:
            setCursor(Qt::OpenHandCursor);
            break;
        }
    }

    QWidget::mouseMoveEvent(e);
}

void PinWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_cropping && e->button() == Qt::LeftButton) {
        // 松手不自动应用裁剪，等用户确认
        update();
        return;
    }

    if (e->button() == Qt::LeftButton) {
        if (m_resizing) {
            m_resizing = false;
            m_resizeCorner = Corner::None;
            setCursor(Qt::OpenHandCursor);
            return;
        }
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
    QWidget::mouseReleaseEvent(e);
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && !m_cropping) {
        // 双击：在 100% 和适应内容之间切换（简化：重置为 100%）
        resetTransform();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void PinWindow::wheelEvent(QWheelEvent *e)
{
    const int delta = e->angleDelta().y();
    if (delta == 0)
        return;

    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);

    if (ctrl) {
        // 透明度
        const qreal step = delta > 0 ? 0.05 : -0.05;
        setOpacityValue(m_opacity + step);
    } else if (shift) {
        // 精细缩放
        zoomBy(delta > 0 ? 1.01 : 1.0 / 1.01);
    } else {
        zoomBy(delta > 0 ? 1.1 : 1.0 / 1.1);
    }
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_1: rotateCW(); return;
    case Qt::Key_2: rotateCCW(); return;
    case Qt::Key_3: flipHorizontal(); return;
    case Qt::Key_4: flipVertical(); return;
    case Qt::Key_5: toggleGrayscale(); return;
    case Qt::Key_6: toggleInverted(); return;

    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (e->modifiers().testFlag(Qt::ControlModifier))
            setOpacityValue(m_opacity + 0.05);
        else
            zoomBy(1.1);
        return;

    case Qt::Key_Minus:
        if (e->modifiers().testFlag(Qt::ControlModifier))
            setOpacityValue(m_opacity - 0.05);
        else
            zoomBy(1.0 / 1.1);
        return;

    case Qt::Key_Escape:
        if (m_cropping) {
            cancelCrop();
            return;
        }
        if (e->modifiers().testFlag(Qt::ShiftModifier))
            emit requestDestroy(this);   // Shift+Esc = 销毁
        else
            emit requestClose(this);
        return;

    case Qt::Key_A:
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            PinManager::instance()->selectAllVisible();
            return;
        }
        break;

    case Qt::Key_T:
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            // Ctrl+T 在贴图窗口上也触发一次贴图
            PinManager::instance()->pasteFromClipboard();
            return;
        }
        break;

    case Qt::Key_C:
        if (e->modifiers().testFlag(Qt::ControlModifier)
            && e->modifiers().testFlag(Qt::ShiftModifier)) {
            // Ctrl+Shift+C：复制原文
            if (!m_sourceText.isEmpty())
                QApplication::clipboard()->setText(m_sourceText);
            return;
        }
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            QApplication::clipboard()->setImage(m_display);
            OperationToast::imageCopied();
            return;
        }
        break;

    case Qt::Key_S:
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("保存图片"),
                QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
            if (!path.isEmpty()) {
                if (ImageOutput::save(m_display, path))
                    OperationToast::imageSaved();
                else
                    qWarning() << "PinWindow: failed to save image to" << path;
            }
            return;
        }
        break;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_cropping) {
            applyCrop();
            return;
        }
        break;

    case Qt::Key_W:
        if (e->modifiers().testFlag(Qt::ControlModifier)) {
            emit requestClose(this);
            return;
        }
        break;

    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int step = e->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        QPoint d;
        if (e->key() == Qt::Key_Left)  d.setX(-step);
        if (e->key() == Qt::Key_Right) d.setX(step);
        if (e->key() == Qt::Key_Up)    d.setY(-step);
        if (e->key() == Qt::Key_Down)  d.setY(step);
        move(pos() + d);
        return;
    }

    default:
        break;
    }

    QWidget::keyPressEvent(e);
}

void PinWindow::contextMenuEvent(QContextMenuEvent *e)
{
    emit contextMenuRequested(this, e->globalPos());
    e->accept();
}

void PinWindow::enterEvent(QEnterEvent *e)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(e);
}

void PinWindow::leaveEvent(QEvent *e)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(e);
}

void PinWindow::changeEvent(QEvent *e)
{
    // 激活/失活时重绘：发光只在激活态显示
    if (e->type() == QEvent::ActivationChange) {
        m_active = isActiveWindow();
        update();
    }
    QWidget::changeEvent(e);
}

void PinWindow::focusOutEvent(QFocusEvent *e)
{
    // 双保险：某些情况下（如点击别的程序）Tool 窗口收不到 ActivationChange，
    // 但会收到 FocusOut。这里也把激活标记清掉。
    m_active = false;
    update();
    QWidget::focusOutEvent(e);
}

void PinWindow::focusInEvent(QFocusEvent *e)
{
    m_active = true;
    update();
    QWidget::focusInEvent(e);
}

void PinWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasImage() || e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void PinWindow::dropEvent(QDropEvent *e)
{
    const QMimeData *mime = e->mimeData();

    if (mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            replaceImage(img);
            e->acceptProposedAction();
            return;
        }
    }

    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const QImage img(url.toLocalFile());
            if (!img.isNull()) {
                replaceImage(img);
                e->acceptProposedAction();
                return;
            }
        }
    }
}

void PinWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
}
