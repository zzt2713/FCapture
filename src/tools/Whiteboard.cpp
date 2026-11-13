#include "tools/Whiteboard.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/shapes/FreehandShape.h"
#include "annotate/shapes/LineShape.h"
#include "annotate/shapes/PixelShape.h"
#include "annotate/shapes/RectShapes.h"
#include "annotate/shapes/TextShape.h"
#include "core/ConfigManager.h"
#include "platform/win/WinUtil.h"
#include "ui/OperationToast.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>

namespace {

constexpr int kMinShape = 3;

// 网格规格（对齐"工程图纸"那种极简淡网格）：
//   格子 32 逻辑像素，线宽 1px
//   竖线比横线略深一点点 —— 这是原图里的细节，让视觉重心偏向纵向
//   颜色对比度压得很低，只作为"若有若无"的底纹
constexpr int    kGridSize        = 32;
constexpr int    kGridMajorEvery  = 4;      // 每 4 格加一条稍深的线，形成大格
constexpr QColor kGridLineH(0xE4, 0xE4, 0xE4);   // 横线：更淡
constexpr QColor kGridLineV(0xDC, 0xDC, 0xDC);   // 竖线：略深
constexpr QColor kGridMajor(0xD2, 0xD2, 0xD2);   // 每 4 格的粗线

} // namespace

Whiteboard::Whiteboard(const QColor &background, bool transparent, QWidget *parent)
    : QWidget(parent)
    , m_model(new AnnotationModel(this))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    if (transparent)
        setAttribute(Qt::WA_TranslucentBackground);
    else
        setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    m_background = background;
    m_transparent = transparent;

    m_bar = new PaintBar(this);
    connect(m_bar, &PaintBar::toolChanged, this, [this](Tool tool) {
        m_tool = tool;
        cancelShape();
        update();
    });
    connect(m_bar, &PaintBar::toolCleared, this, [this]() {
        m_tool = Tool::None;
        cancelShape();
        update();
    });
    connect(m_bar, &PaintBar::undoRequested, this, [this]() {
        m_model->undo();
        update();
    });
    connect(m_bar, &PaintBar::redoRequested, this, [this]() {
        m_model->redo();
        update();
    });
    connect(m_bar, &PaintBar::clearRequested, this, [this]() {
        m_model->clear();
        update();
    });
    connect(m_bar, &PaintBar::copyRequested, this, &Whiteboard::copyToClipboard);
    connect(m_bar, &PaintBar::saveRequested, this, &Whiteboard::saveToFile);
    connect(m_bar, &PaintBar::closeRequested, this, [this]() {
        emit closed();
        close();
    });
    connect(m_bar, &PaintBar::colorChanged, this, [this]() { update(); });
    connect(m_bar, &PaintBar::penWidthChanged, this, [this]() { update(); });
    connect(m_model, &AnnotationModel::undoStateChanged, m_bar, &PaintBar::setCanUndoRedo);
}

Whiteboard::~Whiteboard() = default;

void Whiteboard::showOnActiveScreen()
{
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen)
        setGeometry(screen->geometry());

    show();
    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    WinUtil::applyToolWindowStyle(winId());
    syncToolbarPosition();
}

// ---------------------------------------------------------------- 绘制

void Whiteboard::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (m_transparent) {
        // 透明白板：不画底色，网格也不画（网格是"纸张"，透明版就是玻璃）
    } else {
        paintGridBackground(p, rect());
    }

    m_model->render(&p, QRectF(rect()));

    if (m_inProgress)
        m_inProgress->paint(&p);
}

// 网格底纹。线条用 0.5 偏移让 1px 线落在像素中心，避免发虚。
void Whiteboard::paintGridBackground(QPainter &p, const QRect &area) const
{
    p.save();
    // 底色铺满整个绘制设备的可见区域（导出时 painter 可能已被 scale，
    // 用 device 尺寸而不是逻辑 area，避免高 DPI 下只填了 1/dpr 的一块）
    p.save();
    p.resetTransform();
    p.fillRect(QRect(0, 0, p.device()->width(), p.device()->height()), m_background);
    p.restore();

    // 网格不需要抗锯齿：1px 直线开着 AA 反而会变成两条半透明灰线
    p.setRenderHint(QPainter::Antialiasing, false);

    const int step = kGridSize;
    const int left = area.left();
    const int right = area.right();
    const int top = area.top();
    const int bottom = area.bottom();

    // 竖线（略深）
    for (int x = left; x <= right; x += step) {
        const bool major = ((x - left) / step) % kGridMajorEvery == 0;
        p.setPen(QPen(major ? kGridMajor : kGridLineV, 1));
        p.drawLine(x, top, x, bottom);
    }
    // 横线（更淡）
    for (int y = top; y <= bottom; y += step) {
        const bool major = ((y - top) / step) % kGridMajorEvery == 0;
        p.setPen(QPen(major ? kGridMajor : kGridLineH, 1));
        p.drawLine(left, y, right, y);
    }

    p.restore();
}

// ---------------------------------------------------------------- 输入

std::shared_ptr<Annotation> Whiteboard::createShapeForTool(Tool tool) const
{
    switch (tool) {
    case Tool::Rectangle: return std::make_shared<RectShape>(RectShape::Form::Rectangle);
    case Tool::Ellipse:   return std::make_shared<RectShape>(RectShape::Form::Ellipse);
    case Tool::Line:      return std::make_shared<LineShape>(LineShape::Kind::Line);
    case Tool::Arrow:     return std::make_shared<LineShape>(LineShape::Kind::Arrow);
    case Tool::Polyline:  return std::make_shared<LineShape>(LineShape::Kind::Polyline);
    case Tool::Pencil:    return std::make_shared<FreehandShape>(FreehandShape::Kind::Pencil);
    case Tool::Marker:    return std::make_shared<FreehandShape>(FreehandShape::Kind::Marker);
    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser: {
        // 白板没有底图，马赛克/模糊无意义；橡皮擦退化为"用背景色画"
        const auto kind = tool == Tool::Eraser ? PixelShape::Kind::Eraser
                                               : (tool == Tool::Mosaic ? PixelShape::Kind::Mosaic
                                                                       : PixelShape::Kind::Blur);
        return std::make_shared<PixelShape>(kind);
    }
    default:
        return nullptr;
    }
}

void Whiteboard::applyToolStyleTo(std::shared_ptr<Annotation> shape) const
{
    if (!shape)
        return;

    const ToolStyle st = ToolStyleStore::instance()->style(m_tool);
    shape->setColor(st.color);
    shape->setPenWidth(st.penWidth);

    if (auto *rect = dynamic_cast<RectShape *>(shape.get())) {
        rect->setPenStyle(st.penStyle);
        if (shape->type() == Annotation::Type::Rectangle)
            rect->setCornerRadius(st.cornerRadius);
    } else if (auto *line = dynamic_cast<LineShape *>(shape.get())) {
        line->setPenStyle(st.penStyle);
        line->setArrowStyle(static_cast<LineShape::ArrowStyle>(st.arrowStyle));
    } else if (auto *fh = dynamic_cast<FreehandShape *>(shape.get())) {
        fh->setPenStyle(st.penStyle);
        fh->setEllipseBrush(st.ellipseBrush);
    } else if (auto *px = dynamic_cast<PixelShape *>(shape.get())) {
        px->setMode(PixelShape::Mode::Brush);
        px->setEllipseBrush(st.ellipseBrush);
    }
}

void Whiteboard::beginShape(const QPoint &pos)
{
    m_inProgress = createShapeForTool(m_tool);
    if (!m_inProgress) {
        m_tool = Tool::None;
        return;
    }

    applyToolStyleTo(m_inProgress);
    m_inProgress->setFinished(false);
    m_shapeStart = pos;
    m_polylinePoints.clear();
    m_polylineActive = (m_tool == Tool::Polyline);

    if (auto *rect = dynamic_cast<RectShape *>(m_inProgress.get()))
        rect->setRect(QRectF(QPointF(pos), QSizeF(0, 0)));
    else if (auto *line = dynamic_cast<LineShape *>(m_inProgress.get()))
        line->setPoints({QPointF(pos), QPointF(pos)});
    else if (auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get()))
        fh->appendPoint(pos);
    else if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get()))
        px->appendPoint(pos);

    if (m_polylineActive)
        m_polylinePoints.append(QPointF(pos));

    update();
}

void Whiteboard::updateShape(const QPoint &pos)
{
    if (!m_inProgress)
        return;

    if (auto *rect = dynamic_cast<RectShape *>(m_inProgress.get())) {
        rect->setRect(QRectF(m_shapeStart, pos).normalized());
    } else if (auto *line = dynamic_cast<LineShape *>(m_inProgress.get())) {
        if (m_polylineActive) {
            QVector<QPointF> pts = m_polylinePoints;
            pts.append(QPointF(pos));
            line->setPoints(pts);
        } else {
            line->setLastPoint(pos);
        }
    } else if (auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get())) {
        fh->appendPoint(pos);
    } else if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get())) {
        px->appendPoint(pos);
    }
    update();
}

void Whiteboard::finishShape()
{
    if (!m_inProgress) {
        m_tool = Tool::None;
        return;
    }

    if (m_polylineActive) {
        auto *line = dynamic_cast<LineShape *>(m_inProgress.get());
        if (line)
            line->setPoints(m_polylinePoints);
        m_polylineActive = false;
    }

    // 太小的形状当误点丢掉
    const QRectF bounds = m_inProgress->boundingRect();
    if (bounds.width() >= kMinShape || bounds.height() >= kMinShape) {
        m_inProgress->setFinished(true);
        m_model->addShape(m_inProgress);
    }
    m_inProgress.reset();
    m_polylinePoints.clear();
    update();
}

void Whiteboard::cancelShape()
{
    m_inProgress.reset();
    m_polylinePoints.clear();
    m_polylineActive = false;
    update();
}

void Whiteboard::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        if (m_inProgress) {
            cancelShape();
            return;
        }
        emit closed();
        close();
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    if (m_tool == Tool::None)
        return;

    beginShape(e->position().toPoint());
}

void Whiteboard::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_inProgress)
        return;
    updateShape(e->position().toPoint());
}

void Whiteboard::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || !m_inProgress)
        return;

    if (m_polylineActive) {
        // 折线：单击加点，双击/右键结束
        m_polylinePoints.append(QPointF(e->position().toPoint()));
        return;
    }
    finishShape();
}

void Whiteboard::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_polylineActive)
        finishShape();
}

void Whiteboard::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_inProgress)
            cancelShape();
        else {
            emit closed();
            close();
        }
        return;
    case Qt::Key_Space:
        toggleToolbar();
        return;
    case Qt::Key_S:
        if (e->modifiers() & Qt::ControlModifier) {
            saveToFile();
            return;
        }
        break;
    case Qt::Key_C:
        if (e->modifiers() & Qt::ControlModifier) {
            copyToClipboard();
            return;
        }
        break;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) {
            m_model->undo();
            update();
            return;
        }
        break;
    case Qt::Key_Y:
        if (e->modifiers() & Qt::ControlModifier) {
            m_model->redo();
            update();
            return;
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_polylineActive)
            finishShape();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void Whiteboard::wheelEvent(QWheelEvent *e)
{
    // 白板上滚轮不做缩放，交给工具栏
    e->ignore();
}

// ---------------------------------------------------------------- 工具栏

void Whiteboard::toggleToolbar()
{
    if (m_bar->isVisible())
        m_bar->hide();
    else {
        syncToolbarPosition();
        m_bar->show();
        m_bar->raise();
    }
}

void Whiteboard::syncToolbarPosition()
{
    if (!m_bar)
        return;

    const QSize hint = m_bar->size();
    const int x = (width() - hint.width()) / 2;
    const int y = height() - hint.height() - 40;
    m_bar->move(qMax(0, x), qMax(0, y));
}

// ---------------------------------------------------------------- 输出

QImage Whiteboard::renderImage() const
{
    QImage img(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(devicePixelRatioF());
    img.fill(m_transparent ? Qt::transparent : m_background);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // 网格在**逻辑坐标**下画（paintGridBackground 内部按 rect() 布局），
    // 所以先不缩放；画完再缩放给标注用。否则 dpr=1.5 时网格会被放大 1.5 倍。
    if (!m_transparent)
        paintGridBackground(p, rect());

    p.scale(devicePixelRatioF(), devicePixelRatioF());

    m_model->render(&p, QRectF(rect()));
    p.end();

    return img;
}

void Whiteboard::copyToClipboard()
{
    QApplication::clipboard()->setImage(renderImage());
    OperationToast::imageCopied();
}

void Whiteboard::saveToFile()
{
    const QString defaultPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("Whiteboard_%1.png")
                          .arg(QDateTime::currentDateTime().toString(
                              QStringLiteral("yyyy-MM-dd_HH-mm-ss"))));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存白板"), defaultPath, QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;

    if (renderImage().save(path))
        OperationToast::imageSaved();
    else
        qWarning() << "Whiteboard: failed to save image to" << path;
}
