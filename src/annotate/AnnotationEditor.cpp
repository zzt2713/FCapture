#include "annotate/AnnotationEditor.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/shapes/FreehandShape.h"
#include "annotate/shapes/LineShape.h"
#include "annotate/shapes/PixelShape.h"
#include "annotate/shapes/RectShapes.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace {

constexpr int kMinShape = 3;

} // namespace

AnnotationEditor::AnnotationEditor(QWidget *parent)
    : QWidget(parent)
    , m_model(new AnnotationModel(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

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
    connect(m_bar, &PaintBar::undoRequested, this, &AnnotationEditor::undo);
    connect(m_bar, &PaintBar::redoRequested, this, &AnnotationEditor::redo);
    connect(m_bar, &PaintBar::clearRequested, this, &AnnotationEditor::clearAll);
    connect(m_bar, &PaintBar::copyRequested, this, &AnnotationEditor::accepted);
    connect(m_bar, &PaintBar::saveRequested, this, &AnnotationEditor::accepted);
    connect(m_bar, &PaintBar::pinRequested, this, &AnnotationEditor::accepted);
    connect(m_bar, &PaintBar::closeRequested, this, &AnnotationEditor::cancelled);
    connect(m_bar, &PaintBar::colorChanged, this, [this]() { update(); });
    connect(m_bar, &PaintBar::penWidthChanged, this, [this]() { update(); });

    connect(m_model, &AnnotationModel::undoStateChanged, m_bar, &PaintBar::setCanUndoRedo);
    connect(m_model, &AnnotationModel::changed, this, [this]() {
        update();
        emit contentChanged();
    });

    m_bar->hide();
}

void AnnotationEditor::render(QPainter *p) const
{
    if (!p)
        return;
    m_model->render(p, QRectF(rect()));
    if (m_inProgress)
        m_inProgress->paint(p);
}

void AnnotationEditor::renderOnto(QImage *canvas, const QRect &targetRect) const
{
    if (!canvas || canvas->isNull() || targetRect.isEmpty() || m_model->isEmpty())
        return;

    QPainter p(canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    // 编辑器本地坐标 → 目标图片像素坐标
    p.scale(qreal(canvas->width()) / targetRect.width(),
            qreal(canvas->height()) / targetRect.height());
    p.translate(-targetRect.topLeft());
    m_model->render(&p, QRectF(targetRect));
    p.end();
}

bool AnnotationEditor::isEmpty() const
{
    return m_model->isEmpty();
}

PaintBar *AnnotationEditor::takeToolbar()
{
    if (!m_bar)
        return nullptr;
    PaintBar *bar = m_bar;
    m_bar = nullptr;   // 所有权交给调用方，析构时不再管它
    bar->hide();
    return bar;
}

void AnnotationEditor::hideToolbar()
{
    if (m_bar)
        m_bar->hide();
}

void AnnotationEditor::positionToolbar(const QRect &anchoredRect)
{
    if (!m_bar)
        return;
    m_bar->positionNear(anchoredRect);
    m_bar->raise();
}

void AnnotationEditor::undo()
{
    m_model->undo();
    update();
}

void AnnotationEditor::redo()
{
    m_model->redo();
    update();
}

void AnnotationEditor::clearAll()
{
    m_model->clear();
    update();
}

// ---------------------------------------------------------------- 绘制

void AnnotationEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    render(&p);
    drawHoverFrame(p);
    drawSelection(p);
}

// 悬停候选框：主色虚线，向外扩 4px（对齐 snow 的 SelectionCandidateFrame）
void AnnotationEditor::drawHoverFrame(QPainter &p) const
{
    if (!m_hoveredShape || m_hoveredShape->isDegenerate())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor accent = Theme::kAccent;
    accent.setAlpha(170);
    QPen pen(accent, 1.0, Qt::DashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(m_hoveredShape->boundingRect());

    p.restore();
}

// 选中框 + 锚点（对齐 snow 的 SelectionFrame / SelectionResizeHandle）
void AnnotationEditor::drawSelection(QPainter &p) const
{
    if (m_selectedShapes.empty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool multi = m_selectedShapes.size() > 1;

    for (const auto &shape : m_selectedShapes) {
        if (!shape)
            continue;

        // 单选实线、多选虚线；线宽 1px（snow 的 1.0 / zoom）
        QPen framePen(Theme::kAccent, 1.0);
        if (multi) {
            framePen.setStyle(Qt::DashLine);
            framePen.setDashPattern({ 4.0, 3.0 });
        }
        p.setPen(framePen);
        p.setBrush(Qt::NoBrush);

        // 直接贴着 boundingRect 画：它已经含 penWidth/2 + 1，
        // 再外扩会让框离图形太远（snow 的 padding 是相对几何边界算的）。
        const QRectF box = shape->boundingRect();
        if (shape->isRotatable() && !qFuzzyIsNull(shape->rotation())) {
            const QPolygonF poly = shape->rotationTransform().map(
                QPolygonF(QRectF(shape->rawBoundingRect())));
            p.drawPolygon(poly);
        } else {
            p.drawRect(box);
        }

        if (multi)
            continue;

        // 单选才画锚点
        const QVector<QPointF> handles = shape->handlePositions();
        const int rotIndex = shape->rotationHandleIndex();
        const bool hasRotation = rotIndex >= 0 && rotIndex < handles.size();
        constexpr qreal kHandleRadius = 4.0;
        constexpr qreal kHandleStroke = 1.0;
        const int count = hasRotation ? rotIndex : handles.size();

        p.setPen(QPen(Qt::white, kHandleStroke));
        p.setBrush(Theme::kAccent);
        for (int i = 0; i < count; ++i)
            p.drawEllipse(handles.at(i), kHandleRadius, kHandleRadius);

        if (hasRotation) {
            const QPointF rot = handles.at(rotIndex);
            constexpr qreal kHalf = 4.0;
            p.setPen(QPen(Qt::white, kHandleStroke));
            p.setBrush(Theme::kAccent);
            p.drawRoundedRect(QRectF(rot.x() - kHalf, rot.y() - kHalf,
                                     kHalf * 2, kHalf * 2), 2, 2);
        }

        // 圆角手柄：空心小圆
        for (const QPointF &rh : cornerRadiusHandlePositions(shape)) {
            p.setPen(QPen(Theme::kAccent, 1.6));
            p.setBrush(Qt::white);
            p.drawEllipse(rh, 3.5, 3.5);
        }
    }

    p.restore();
}

// ---------------------------------------------------------------- 形状

std::shared_ptr<Annotation> AnnotationEditor::createShapeForTool(Tool tool) const
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
        const auto kind = tool == Tool::Eraser ? PixelShape::Kind::Eraser
                                               : (tool == Tool::Mosaic ? PixelShape::Kind::Mosaic
                                                                       : PixelShape::Kind::Blur);
        return std::make_shared<PixelShape>(kind);
    }
    default:
        return nullptr;
    }
}

void AnnotationEditor::applyToolStyleTo(const std::shared_ptr<Annotation> &shape) const
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

void AnnotationEditor::beginShape(const QPoint &pos)
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
        line->setPoints({ QPointF(pos), QPointF(pos) });
    else if (auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get()))
        fh->appendPoint(pos);
    else if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get()))
        px->appendPoint(pos);

    if (m_polylineActive)
        m_polylinePoints.append(QPointF(pos));

    update();
}

void AnnotationEditor::updateShape(const QPoint &pos)
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

void AnnotationEditor::finishShape()
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

void AnnotationEditor::cancelShape()
{
    m_inProgress.reset();
    m_polylinePoints.clear();
    m_polylineActive = false;
    update();
}

// ---------------------------------------------------------------- 选中 / 编辑

void AnnotationEditor::selectShapeAt(const QPoint &pos, bool additive)
{
    const auto hit = m_model->hitTestShape(QPointF(pos), 5.0);

    if (!additive) {
        for (const auto &shape : m_selectedShapes)
            if (shape)
                shape->setSelected(false);
        m_selectedShapes.clear();
    }

    if (!hit) {
        update();
        return;
    }

    auto it = std::find(m_selectedShapes.begin(), m_selectedShapes.end(), hit);
    if (additive && it != m_selectedShapes.end()) {
        hit->setSelected(false);
        m_selectedShapes.erase(it);
    } else {
        if (it == m_selectedShapes.end())
            m_selectedShapes.push_back(hit);
        hit->setSelected(true);
    }
    update();
}

void AnnotationEditor::clearSelection()
{
    for (const auto &shape : m_selectedShapes)
        if (shape)
            shape->setSelected(false);
    m_selectedShapes.clear();
    update();
}

void AnnotationEditor::updateHoveredShape(const QPoint &pos)
{
    std::shared_ptr<Annotation> next;
    // 有选中项时不再显示候选框（和截图层的规则一致）
    if (m_selectedShapes.empty() && m_state == EditorState::Idle)
        next = m_model->hitTestShape(QPointF(pos), 5.0);

    if (next == m_hoveredShape)
        return;
    m_hoveredShape = next;
    update();
}

bool AnnotationEditor::hitTestSelectedHandle(const QPoint &pos, int *outIndex) const
{
    if (m_selectedShapes.size() != 1)
        return false;
    const auto &shape = m_selectedShapes.front();
    if (!shape)
        return false;

    const QVector<QPointF> handles = shape->handlePositions();
    const int rotIndex = shape->rotationHandleIndex();
    constexpr qreal kTolerance = 8.0;
    for (int i = 0; i < handles.size(); ++i) {
        if (i == rotIndex)
            continue;
        const QPointF d = handles.at(i) - QPointF(pos);
        if (QPointF::dotProduct(d, d) <= kTolerance * kTolerance) {
            if (outIndex)
                *outIndex = i;
            return true;
        }
    }
    return false;
}

bool AnnotationEditor::hitTestRotationHandle(const QPoint &pos) const
{
    if (m_selectedShapes.size() != 1)
        return false;
    const auto &shape = m_selectedShapes.front();
    if (!shape || !shape->isRotatable())
        return false;

    const QVector<QPointF> handles = shape->handlePositions();
    const int rotIndex = shape->rotationHandleIndex();
    if (rotIndex < 0 || rotIndex >= handles.size())
        return false;

    const QPointF d = handles.at(rotIndex) - QPointF(pos);
    return QPointF::dotProduct(d, d) <= 8.0 * 8.0;
}

qreal AnnotationEditor::maxCornerRadiusFor(const std::shared_ptr<Annotation> &shape) const
{
    if (!shape)
        return 0.0;
    const QRectF r = shape->rawBoundingRect();
    return qMax<qreal>(0.0, qMin(r.width(), r.height()) / 2.0);
}

QVector<QPointF> AnnotationEditor::cornerRadiusHandlePositions(
    const std::shared_ptr<Annotation> &shape) const
{
    QVector<QPointF> out;
    if (!shape || shape->type() != Annotation::Type::Rectangle || m_selectedShapes.size() != 1)
        return out;

    const QRectF r = shape->rawBoundingRect();
    const qreal maxRadius = maxCornerRadiusFor(shape);
    if (maxRadius <= 0.0)
        return out;

    const auto *rect = dynamic_cast<const RectShape *>(shape.get());
    const qreal currentRadius = rect ? rect->cornerRadius() : 0.0;
    const qreal radius = qMin(qMax<qreal>(0.0, currentRadius), maxRadius);

    const qreal minInset = 12.0;
    const qreal maxInset = qMax(minInset, qMin(r.width(), r.height()) / 2.0 - 4.0);
    const qreal t = maxRadius > 0.0 ? radius / maxRadius : 0.0;
    const qreal inset = minInset + (maxInset - minInset) * t;

    const QPointF c = r.center();
    out.append(QPointF(c.x() - r.width() / 2.0 + inset, c.y() - r.height() / 2.0 + inset));
    out.append(QPointF(c.x() + r.width() / 2.0 - inset, c.y() - r.height() / 2.0 + inset));
    out.append(QPointF(c.x() + r.width() / 2.0 - inset, c.y() + r.height() / 2.0 - inset));
    out.append(QPointF(c.x() - r.width() / 2.0 + inset, c.y() + r.height() / 2.0 - inset));
    return out;
}

bool AnnotationEditor::hitTestCornerRadiusHandle(const QPoint &pos, int *outCorner) const
{
    if (m_selectedShapes.size() != 1)
        return false;
    const auto &shape = m_selectedShapes.front();
    const QVector<QPointF> positions = cornerRadiusHandlePositions(shape);
    constexpr qreal kTolerance = 9.0;
    for (int i = 0; i < positions.size(); ++i) {
        const QPointF d = positions.at(i) - QPointF(pos);
        if (QPointF::dotProduct(d, d) <= kTolerance * kTolerance) {
            if (outCorner)
                *outCorner = i;
            return true;
        }
    }
    return false;
}

// 光标：按命中的对象给出不同形状（对齐 snow 的 CursorStyle 语义）。
// 注意：没有选中工具时默认是普通箭头 —— 编辑态下用户是在"选/拖"，
// 不是"落笔"，显示十字会误导。
void AnnotationEditor::updateCursorFor(const QPoint &pos)
{
    if (m_tool == Tool::None) {
        if (hitTestCornerRadiusHandle(pos, nullptr)) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
        if (hitTestRotationHandle(pos)) {
            setCursor(Qt::CrossCursor);
            return;
        }
        int index = -1;
        if (hitTestSelectedHandle(pos, &index)) {
            const bool isCorner = (index % 2) == 0;
            if (isCorner)
                setCursor(Qt::SizeFDiagCursor);
            else if (index == 1 || index == 5)
                setCursor(Qt::SizeVerCursor);
            else
                setCursor(Qt::SizeHorCursor);
            return;
        }
        for (const auto &shape : m_selectedShapes) {
            if (shape && shape->hitTest(QPointF(pos), 5.0)) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
        if (m_model->hitTestShape(QPointF(pos), 5.0)) {
            setCursor(Qt::SizeAllCursor);
            return;
        }
        setCursor(Qt::ArrowCursor);
        return;
    }

    // 有工具：该工具的落笔光标
    setCursor(m_tool == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
}

// ---------------------------------------------------------------- 输入

void AnnotationEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    const QPoint pos = e->position().toPoint();
    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);

    // ---- 1) 先判已选中形状的手柄（圆角 > 旋转 > 缩放）----
    if (!m_selectedShapes.empty() && m_tool == Tool::None) {
        int corner = -1;
        if (hitTestCornerRadiusHandle(pos, &corner)) {
            m_cornerRadiusCorner = corner;
            m_activeShape = m_selectedShapes.front();
            m_state = EditorState::Resizing;
            m_dragStart = pos;
            return;
        }
        if (hitTestRotationHandle(pos)) {
            m_activeShape = m_selectedShapes.front();
            m_state = EditorState::Rotating;
            m_rotateStartAngle =
                QLineF(m_activeShape->boundingRect().center(), QPointF(pos)).angle();
            m_rotateStartRotation = m_activeShape->rotation();
            m_dragStart = pos;
            return;
        }
        int handle = -1;
        if (hitTestSelectedHandle(pos, &handle)) {
            m_activeHandle = handle;
            m_activeShape = m_selectedShapes.front();
            m_state = EditorState::Resizing;
            m_dragStart = pos;
            if (m_activeShape)
                m_activeShape->beginHandleDrag();
            return;
        }
    }

    // ---- 2) 快速选择：点到已有标注就连同工具激活时也先选中（snow 同款）----
    if (m_state == EditorState::Idle && m_tool != Tool::Eraser) {
        if (const auto hitShape = m_model->hitTestShape(QPointF(pos), 5.0)) {
            selectShapeAt(pos, ctrl);
            m_activeShape = hitShape;
            m_state = EditorState::Moving;
            m_dragStart = pos;
            m_dragOrigin = hitShape->boundingRect().topLeft();
            return;
        }
    }

    // ---- 3) 点空白：有选中就取消，否则开始画 ----
    if (!m_selectedShapes.empty()) {
        clearSelection();
        if (m_tool == Tool::None) {
            update();
            return;
        }
    }

    if (m_tool == Tool::None) {
        update();
        return;
    }

    m_state = EditorState::Drawing;
    beginShape(pos);
}

void AnnotationEditor::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();

    switch (m_state) {
    case EditorState::Drawing:
        updateShape(pos);
        return;

    case EditorState::Moving:
        if (m_activeShape) {
            const QPointF delta = QPointF(pos) - QPointF(m_dragStart);
            m_activeShape->translate(delta - (m_activeShape->boundingRect().topLeft()
                                              - m_dragOrigin));
            m_activeShape->invalidateCache();
            update();
        }
        return;

    case EditorState::Resizing:
        if (m_activeShape && m_cornerRadiusCorner >= 0) {
            auto *rectShape = dynamic_cast<RectShape *>(m_activeShape.get());
            if (rectShape) {
                const QRectF r = m_activeShape->rawBoundingRect();
                const bool rightSide = (m_cornerRadiusCorner == 1 || m_cornerRadiusCorner == 2);
                const bool bottomSide = (m_cornerRadiusCorner == 2 || m_cornerRadiusCorner == 3);
                const QPointF corner(rightSide ? r.right() : r.left(),
                                     bottomSide ? r.bottom() : r.top());
                const qreal dist = QLineF(corner, QPointF(pos)).length();
                rectShape->setCornerRadius(
                    qBound<qreal>(0.0, dist, maxCornerRadiusFor(m_activeShape)));
                m_activeShape->invalidateCache();
                update();
            }
        } else if (m_activeShape && m_activeHandle >= 0) {
            m_activeShape->moveHandle(m_activeHandle, QPointF(pos));
            m_activeShape->invalidateCache();
            update();
        }
        return;

    case EditorState::Rotating:
        if (m_activeShape) {
            const qreal angle =
                QLineF(m_activeShape->boundingRect().center(), QPointF(pos)).angle();
            m_activeShape->setRotation(m_rotateStartRotation - (angle - m_rotateStartAngle));
            m_activeShape->invalidateCache();
            update();
        }
        return;

    case EditorState::Idle:
        break;
    }

    // 空闲：更新悬停候选框 + 光标
    updateHoveredShape(pos);
    updateCursorFor(pos);
}

void AnnotationEditor::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    switch (m_state) {
    case EditorState::Drawing:
        if (m_polylineActive) {
            m_polylinePoints.append(QPointF(e->position().toPoint()));
            return;
        }
        finishShape();
        break;

    case EditorState::Moving:
    case EditorState::Rotating:
        m_state = EditorState::Idle;
        m_activeShape.reset();
        update();
        break;

    case EditorState::Resizing:
        if (m_activeShape)
            m_activeShape->endHandleDrag();
        m_activeShape.reset();
        m_activeHandle = -1;
        m_cornerRadiusCorner = -1;
        m_state = EditorState::Idle;
        update();
        break;

    case EditorState::Idle:
        break;
    }
}

void AnnotationEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_polylineActive)
        finishShape();
}

// 选中标注的右键菜单（置顶 / 置底 / 删除）。
// 这里用原生 QMenu：annotate 层不允许引 Ela 头文件。
void AnnotationEditor::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_selectedShapes.empty()) {
        e->ignore();
        return;
    }

    // 只有右键落在选中标注上才弹菜单，点空白仍走"取消选中"
    bool onSelected = false;
    for (const auto &shape : m_selectedShapes) {
        if (shape && shape->hitTest(QPointF(e->pos()), 5.0)) {
            onSelected = true;
            break;
        }
    }
    if (!onSelected) {
        e->ignore();
        return;
    }

    QMenu menu(this);
    QAction *front = menu.addAction(QStringLiteral("置于顶层"));
    QAction *back = menu.addAction(QStringLiteral("置于底层"));
    menu.addSeparator();
    const QString delText = m_selectedShapes.size() > 1
                                ? QStringLiteral("删除选中的 %1 个标注").arg(m_selectedShapes.size())
                                : QStringLiteral("删除标注");
    QAction *remove = menu.addAction(delText);

    QAction *chosen = menu.exec(e->globalPos());
    if (chosen == front) {
        for (const auto &shape : m_selectedShapes)
            m_model->bringToFront(shape);
        update();
    } else if (chosen == back) {
        for (const auto &shape : m_selectedShapes)
            m_model->sendToBack(shape);
        update();
    } else if (chosen == remove) {
        const auto shapes = m_selectedShapes;
        for (const auto &shape : shapes)
            m_model->removeShape(shape);
        m_selectedShapes.clear();
        m_hoveredShape.reset();
        update();
    }
    e->accept();
}

void AnnotationEditor::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_inProgress) {
            cancelShape();
        } else if (!m_selectedShapes.empty()) {
            // Esc 逐级退出：先取消选中，再退出编辑
            clearSelection();
            update();
        } else {
            emit cancelled();
        }
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (!m_selectedShapes.empty()) {
            for (const auto &shape : m_selectedShapes)
                m_model->removeShape(shape);
            m_selectedShapes.clear();
            m_hoveredShape.reset();
            update();
            return;
        }
        break;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) {
            undo();
            return;
        }
        break;
    case Qt::Key_Y:
        if (e->modifiers() & Qt::ControlModifier) {
            redo();
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

void AnnotationEditor::wheelEvent(QWheelEvent *e)
{
    // 滚轮留给工具栏，编辑器本身不缩放
    e->ignore();
}
