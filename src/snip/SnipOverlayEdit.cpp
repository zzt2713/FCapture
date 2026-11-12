#include "snip/SnipOverlay.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/shapes/LineShape.h"
#include "annotate/shapes/RectShapes.h"
#include "annotate/shapes/TextShape.h"
#include "ui/Theme.h"

#include <QGuiApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

// SnipOverlay 的折线绘制 + 选中编辑 + 套索 + 克隆实现

// ---------------------------------------------------------------- 折线

void SnipOverlay::beginPolyline(const QPoint &localPos)
{
    auto line = std::make_shared<LineShape>(LineShape::Kind::Polyline);
    applyToolStyleTo(line);
    line->setFinished(false);
    line->setPoints({QPointF(localPos), QPointF(localPos)});

    m_inProgress = line;
    m_polylinePoints.clear();
    m_polylinePoints.append(QPointF(localPos));
    m_polylineCursor = localPos;
    m_state = State::PolylineDrawing;
    update();
}

void SnipOverlay::addPolylinePoint(const QPoint &localPos)
{
    if (!m_inProgress || m_state != State::PolylineDrawing)
        return;

    if (!m_polylinePoints.isEmpty()
        && QLineF(m_polylinePoints.last(), QPointF(localPos)).length() < 3.0)
        return;

    m_polylinePoints.append(QPointF(localPos));

    auto *line = dynamic_cast<LineShape *>(m_inProgress.get());
    if (line) {
        QVector<QPointF> pts = m_polylinePoints;
        pts.append(QPointF(localPos));
        line->setPoints(pts);
    }
    update();
}

void SnipOverlay::updatePolylinePreview(const QPoint &localPos)
{
    if (!m_inProgress || m_state != State::PolylineDrawing)
        return;

    m_polylineCursor = localPos;

    auto *line = dynamic_cast<LineShape *>(m_inProgress.get());
    if (line) {
        QVector<QPointF> pts = m_polylinePoints;
        pts.append(QPointF(localPos));
        line->setPoints(pts);
    }
    update();
}

void SnipOverlay::finishPolyline()
{
    if (!m_inProgress) {
        m_state = State::Adjusting;
        return;
    }

    auto *line = dynamic_cast<LineShape *>(m_inProgress.get());
    if (line)
        line->setPoints(m_polylinePoints);

    const bool enough = m_polylinePoints.size() >= 2;
    if (enough) {
        m_inProgress->setFinished(true);
        insertCreatedShape(m_inProgress);
    }

    m_inProgress.reset();
    m_polylinePoints.clear();
    m_state = State::Adjusting;

    update();
}

// ---------------------------------------------------------------- 光标

// 对齐 snow 的 ToolPolicy::default_cursor（policy.rs）：
//   Select          → Default（普通箭头）
//   形状/箭头/线    → Crosshair
//   Text            → Text（I 形）
//   Eraser          → 十字（Qt 没有专门的橡皮光标，用十字最接近）
//   SerialNumber    → Crosshair
//   Watermark       → Default
// 其余绘画类一律十字。
Qt::CursorShape SnipOverlay::toolCursor() const
{
    switch (m_currentTool) {
    case Tool::None:
        // 没有激活标注工具 = 用户在框选截图区域，必须是十字。
        // 注意这和 snow 的 Select 工具不同：那个是画布内"选择已有标注"，
        // 而这里的 Tool::None 是截图选区，两者不能混为一谈。
        return Qt::CrossCursor;
    case Tool::Lasso:
        return Qt::ArrowCursor;    // 套索 = snow 的 Select 工具
    case Tool::Text:
        return Qt::IBeamCursor;    // snow 的 CursorStyle::Text
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Line:
    case Tool::Arrow:
    case Tool::Polyline:
    case Tool::Magnify:
    case Tool::Number:
    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser:
    case Tool::Pencil:
    case Tool::Marker:
        return Qt::CrossCursor;
    }
    return Qt::CrossCursor;
}

// ---------------------------------------------------------------- 悬停候选

// snow 的 hovered element：鼠标经过某个标注时，即使还没点，
// 也画一圈虚线框提示"这里可以选中"。只在没有选中任何图形时生效，
// 否则会和选中框叠在一起。
void SnipOverlay::updateHoveredShape(const QPoint &localPos)
{
    std::shared_ptr<Annotation> next;

    // 有选中项时不再显示候选框：用户已经在编辑了，再加一层框只会乱
    if (m_selectedShapes.empty() && m_state == State::Adjusting) {
        next = m_model->hitTestShape(QPointF(localPos), 4.0);
    }

    if (next == m_hoveredShape)
        return;
    m_hoveredShape = next;
    update();
}

void SnipOverlay::drawHoveredShapeFrame(QPainter &p)
{
    if (!m_hoveredShape || m_hoveredShape->isDegenerate())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 对齐 snow 的 SelectionCandidateFrame：1px 虚线框，主色半透明。
    // 同样不再额外外扩：boundingRect 已含笔宽，再加一圈会离图形太远。
    QColor accent = Theme::kAccent;
    accent.setAlpha(170);

    QPen pen(accent, 1.0, Qt::DashLine);
    pen.setDashPattern({ 4.0, 3.0 });
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QRectF box = m_hoveredShape->boundingRect();
    p.drawRect(box);

    p.restore();
}

// ---------------------------------------------------------------- 选中

void SnipOverlay::selectShapeAt(const QPoint &localPos, bool additive)
{
    const auto hit = m_model->hitTestShape(QPointF(localPos), 4.0);

    if (!additive)
        clearSelectionSilently();

    if (!hit)
        return;

    const auto it = std::find(m_selectedShapes.begin(), m_selectedShapes.end(), hit);
    if (additive && it != m_selectedShapes.end()) {
        m_selectedShapes.erase(it);   // Ctrl+点击已选中的 → 取消选中
        hit->setSelected(false);
    } else {
        // 普通单击也必须登记到 m_selectedShapes：
        // 只设 shape->setSelected(true) 而列表为空的话，
        // mousePressEvent 里"点空白 → clearSelection()"那段永远进不去，
        // 选中状态会卡死无法取消，锚点/移动/删除也全部失效。
        if (it == m_selectedShapes.end())
            m_selectedShapes.push_back(hit);
        hit->setSelected(true);
    }
    syncTextStyleBar();
    update();
}

void SnipOverlay::clearSelection()
{
    if (m_selectedShapes.empty())
        return;
    clearSelectionSilently();
    syncTextStyleBar();
    update();
}

void SnipOverlay::deleteSelected()
{
    if (m_selectedShapes.empty())
        return;

    const auto shapes = m_selectedShapes;
    m_selectedShapes.clear();
    for (const auto &s : shapes)
        m_model->removeShape(s);
    syncTextStyleBar();
    update();
}

bool SnipOverlay::hitTestSelectedHandle(const QPoint &localPos, int *outIndex,
                                        std::shared_ptr<Annotation> *outShape) const
{
    const int tolerance = 8;

    // 单选：常规的 8 锚点 + 旋转点
    for (const auto &shape : m_selectedShapes) {
        const QVector<QPointF> handles = shape->handlePositions();
        const int rotIndex = shape->rotationHandleIndex();
        for (int i = 0; i < handles.size(); ++i) {
            if (i == rotIndex)
                continue;   // 旋转手柄由 hitTestRotationHandle 单独处理
            const QPointF d = handles.at(i) - QPointF(localPos);
            if (QPointF::dotProduct(d, d) <= tolerance * tolerance) {
                if (outIndex)
                    *outIndex = i;
                if (outShape)
                    *outShape = shape;
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- 圆角手柄

// 只有"单选一个矩形"才给圆角手柄：椭圆没有圆角概念，
// 多选时每个形状分别调圆角没有意义，也和 snow 一致。
qreal SnipOverlay::maxCornerRadiusFor(const std::shared_ptr<Annotation> &shape) const
{
    if (!shape)
        return 0.0;
    const QRectF r = shape->rawBoundingRect();
    return qMax<qreal>(0.0, qMin(r.width(), r.height()) / 2.0);
}

QVector<QPointF> SnipOverlay::cornerRadiusHandlePositions(
    const std::shared_ptr<Annotation> &shape) const
{
    QVector<QPointF> out;
    if (!shape || shape->type() != Annotation::Type::Rectangle || m_selectedShapes.size() != 1)
        return out;

    const QRectF r = shape->rawBoundingRect();
    const qreal maxRadius = maxCornerRadiusFor(shape);
    if (maxRadius <= 0.0)
        return out;

    // 内缩量随当前圆角半径线性变化：半径越大，手柄越靠中心。
    // 这样手柄始终停在圆角的弧顶附近，用户能直观看到"圆角有多大"。
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

bool SnipOverlay::hitTestCornerRadiusHandle(const QPoint &localPos, int *outCorner,
                                            std::shared_ptr<Annotation> *outShape) const
{
    if (m_selectedShapes.size() != 1)
        return false;

    const std::shared_ptr<Annotation> &shape = m_selectedShapes.front();
    const QVector<QPointF> positions = cornerRadiusHandlePositions(shape);
    if (positions.isEmpty())
        return false;

    // 命中范围比视觉半径大一点：手柄只有 8px，精确点中太难
    constexpr qreal kTolerance = 9.0;
    for (int i = 0; i < positions.size(); ++i) {
        const QPointF d = positions.at(i) - QPointF(localPos);
        if (QPointF::dotProduct(d, d) <= kTolerance * kTolerance) {
            if (outCorner)
                *outCorner = i;
            if (outShape)
                *outShape = shape;
            return true;
        }
    }
    return false;
}

bool SnipOverlay::hitTestRotationHandle(const QPoint &localPos,
                                        std::shared_ptr<Annotation> *outShape) const
{
    const int tolerance = 9;
    for (const auto &shape : m_selectedShapes) {
        if (!shape->isRotatable())
            continue;
        const int rotIndex = shape->rotationHandleIndex();
        if (rotIndex < 0)
            continue;
        const QVector<QPointF> handles = shape->handlePositions();
        if (rotIndex >= handles.size())
            continue;
        const QPointF d = handles.at(rotIndex) - QPointF(localPos);
        if (QPointF::dotProduct(d, d) <= tolerance * tolerance) {
            if (outShape)
                *outShape = shape;
            return true;
        }
    }
    return false;
}

void SnipOverlay::beginRotate(const QPoint &localPos)
{
    std::shared_ptr<Annotation> shape;
    if (!hitTestRotationHandle(localPos, &shape))
        return;

    m_rotatingShape = shape;
    const QPointF center = shape->boundingRect().center();
    m_rotateStartAngle = QLineF(center, QPointF(localPos)).angle();
    m_rotateStartRotation = shape->rotation();
    m_state = State::Resizing;
    m_resizeShapeHandleIndex = -1;
    m_activeAnchor = Anchor::None;
}

void SnipOverlay::updateRotate(const QPoint &localPos)
{
    if (!m_rotatingShape)
        return;

    const QPointF center = m_rotatingShape->boundingRect().center();
    const QLineF line(center, QPointF(localPos));
    // Qt 的角度顺时针递增（y 向下），正好和 QPainter::rotate 同向
    const qreal delta = line.angle() - m_rotateStartAngle;
    qreal angle = m_rotateStartRotation + delta;

    if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier))
        angle = qRound(angle / 15.0) * 15.0;

    m_rotatingShape->setRotation(angle);
    update();
}

void SnipOverlay::scaleSelectedByWheel(int delta)
{
    if (m_selectedShapes.empty())
        return;

    // 以整个选中集合的包围盒中心为基准整体缩放
    QRectF total;
    for (const auto &shape : m_selectedShapes)
        total = total.united(shape->boundingRect());
    const QPointF center = total.center();

    const qreal step = delta > 0 ? 1.1 : (1.0 / 1.1);
    const qreal newW = total.width() * step;
    if (newW < 8 || newW > 8000)
        return;

    for (const auto &shape : m_selectedShapes) {
        shape->scaleAbout(center, step);
        shape->invalidateCache();
    }
    update();
}

void SnipOverlay::drawSelectionHandles(QPainter &p)
{
    if (m_selectedShapes.empty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 多选只画包围框；锚点和旋转点只在单选时出现，避免画面过密
    const bool single = (m_selectedShapes.size() == 1);

    for (const auto &shape : m_selectedShapes) {
        drawSelectionBox(p, shape);
        if (!single)
            continue;

        const QVector<QPointF> handles = shape->handlePositions();
        const int rotIndex = shape->rotationHandleIndex();
        const bool hasRotation = rotIndex >= 0 && rotIndex < handles.size();

        // 对齐 snow 的 SelectionResizeHandle / SelectionRotationHandle：
        //   圆形，半径 4，填充主色，白色描边 1.5
        constexpr qreal kHandleRadius = 4.0;
        constexpr qreal kHandleStroke = 1.0;
        const int count = hasRotation ? rotIndex : handles.size();

        p.setPen(QPen(Qt::white, kHandleStroke));
        p.setBrush(Theme::kAccent);
        for (int i = 0; i < count; ++i)
            p.drawEllipse(handles.at(i), kHandleRadius, kHandleRadius);

        if (hasRotation) {
            // 旋转手柄：snow 是方头（SelectionRotationHandle），
            // 和缩放的圆点区分开，一眼能看出"这个点了是转不是缩"
            const QPointF rot = handles.at(rotIndex);
            constexpr qreal kHalf = 4.0;
            p.setPen(QPen(Qt::white, kHandleStroke));
            p.setBrush(Theme::kAccent);
            p.drawRoundedRect(QRectF(rot.x() - kHalf, rot.y() - kHalf,
                                     kHalf * 2, kHalf * 2), 2, 2);
        }

        // 圆角手柄：4 个空心小圆（snow 的 SelectionCornerRadiusHandle），
        // 和实心缩放锚点区分开 —— 拖它改的是圆角，不是尺寸。
        const QVector<QPointF> radiusHandles = cornerRadiusHandlePositions(shape);
        for (const QPointF &rh : radiusHandles) {
            p.setPen(QPen(Theme::kAccent, 1.6));
            p.setBrush(Qt::white);
            p.drawEllipse(rh, 3.5, 3.5);
        }
    }

    p.restore();
}

void SnipOverlay::drawSelectionBox(QPainter &p, const std::shared_ptr<Annotation> &shape)
{
    if (!shape)
        return;

    // 选中框（对齐 snow 的 SelectionFrame / SelectionMultiFrame）：
    //   单选 → 主色实线 1px
    //   多选 → 主色虚线（snow 的 isDashedSelectionFrame 只对 MultiFrame 返回 true）
    // 线宽恒为 1px（snow 用 1.0 / zoom 抵消画布缩放，这里没有画布缩放，直接用 1）
    //
    // 不再额外加 4px padding：rawBoundingRect() 已经把 penWidth/2 + 1 算进去了，
    // 再外扩一圈会让框离图形明显很远。snow 的 padding 是相对**几何边界**的，
    // 我们的 boundingRect 已经是"含笔宽的外沿"，所以这里直接贴着画。
    QPen framePen(Theme::kAccent, 1.0);
    if (m_selectedShapes.size() > 1) {
        framePen.setStyle(Qt::DashLine);
        framePen.setDashPattern({ 4.0, 3.0 });
    }
    p.setPen(framePen);
    p.setBrush(Qt::NoBrush);

    if (shape->isRotatable() && !qFuzzyIsNull(shape->rotation())) {
        // 旋转后的包围盒按四角画
        const QPolygonF poly = shape->rotationTransform().map(
            QPolygonF(QRectF(shape->rawBoundingRect())));
        p.drawPolygon(poly);
    } else {
        p.drawRect(shape->boundingRect());
    }
}

// ---------------------------------------------------------------- 套索 / 框选

void SnipOverlay::beginLasso(const QPoint &localPos)
{
    // 按住拖拽画自由曲线：按下开始记录轨迹，松手自动闭合
    m_lassoPoints.clear();
    m_lassoPoints.append(QPointF(localPos));
    m_lassoCursor = localPos;
    m_state = State::LassoSelecting;
    m_rectSelectActive = false;
    update();
}

void SnipOverlay::addLassoPoint(const QPoint &localPos)
{
    if (m_state != State::LassoSelecting || m_lassoPoints.isEmpty())
        return;
    // 采样间隔 2px：再密只会增加点数，不改善形状
    if (QLineF(m_lassoPoints.last(), QPointF(localPos)).length() < 2.0)
        return;
    m_lassoPoints.append(QPointF(localPos));
    m_lassoCursor = localPos;
    update();
}

void SnipOverlay::closeLasso()
{
    // 轨迹太短 = 误点，直接放弃。
    // 注意不能拿 bounds.isNull() 当"是否首个点"的判断——单点构造的 QRectF
    // 宽高都是 0，isNull() 恒为 true，会把每次迭代都当成第一次，bounds 永远是最后一个点。
    QRectF bounds;
    if (!m_lassoPoints.isEmpty()) {
        qreal minX = m_lassoPoints.first().x();
        qreal maxX = minX;
        qreal minY = m_lassoPoints.first().y();
        qreal maxY = minY;
        for (const QPointF &pt : m_lassoPoints) {
            minX = qMin(minX, pt.x());
            maxX = qMax(maxX, pt.x());
            minY = qMin(minY, pt.y());
            maxY = qMax(maxY, pt.y());
        }
        bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    }

    if (m_lassoPoints.size() < 3 || bounds.width() < 6 || bounds.height() < 6) {
        m_lassoPoints.clear();
        m_state = State::Adjusting;
        update();
        return;
    }

    // 射线法判断中心点是否在多边形内
    auto pointInPolygon = [](const QVector<QPointF> &poly, const QPointF &pt) {
        bool inside = false;
        const int n = poly.size();
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const QPointF &a = poly.at(i);
            const QPointF &b = poly.at(j);
            if (((a.y() > pt.y()) != (b.y() > pt.y()))
                && (pt.x() < (b.x() - a.x()) * (pt.y() - a.y()) / (b.y() - a.y()) + a.x())) {
                inside = !inside;
            }
        }
        return inside;
    };

    // Ctrl 按下 = 追加选择，否则先清空
    const bool additive = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    if (!additive)
        clearSelectionSilently();

    for (const auto &shape : m_model->shapes()) {
        if (!shape)
            continue;
        if (std::find(m_selectedShapes.begin(), m_selectedShapes.end(), shape)
            != m_selectedShapes.end())
            continue;
        if (pointInPolygon(m_lassoPoints, shape->boundingRect().center())) {
            shape->setSelected(true);
            m_selectedShapes.push_back(shape);
        }
    }

    m_lassoPoints.clear();
    m_state = State::Adjusting;
    update();
}

void SnipOverlay::beginRectSelect(const QPoint &localPos)
{
    m_rectSelectAdditive = QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
    m_rectSelectActive = true;
    m_dragStart = localPos;
    m_rectSelectRect = QRect(localPos, QSize(0, 0));
    m_state = State::LassoSelecting;
    m_lassoPoints.clear();
    update();
}

void SnipOverlay::updateRectSelect(const QPoint &localPos)
{
    if (m_state != State::LassoSelecting)
        return;
    m_rectSelectRect = QRect(m_dragStart, localPos).normalized();
    update();
}

void SnipOverlay::finishRectSelect()
{
    if (!m_rectSelectAdditive)
        clearSelectionSilently();

    for (const auto &shape : m_model->shapes()) {
        if (!shape)
            continue;
        if (!QRectF(m_rectSelectRect).contains(shape->boundingRect().center()))
            continue;
        if (std::find(m_selectedShapes.begin(), m_selectedShapes.end(), shape)
            != m_selectedShapes.end())
            continue;
        shape->setSelected(true);
        m_selectedShapes.push_back(shape);
    }

    m_rectSelectRect = QRect();
    m_rectSelectActive = false;
    m_state = State::Adjusting;
    update();
}

// ---------------------------------------------------------------- 复制 / 粘贴 / 克隆

void SnipOverlay::copySelectedToInternal()
{
    if (m_selectedShapes.empty())
        return;

    // 存副本：之后原形状被移动/删除也不影响剪贴板内容
    m_internalShapes.clear();
    for (const auto &shape : m_selectedShapes) {
        if (shape)
            m_internalShapes.push_back(shape->clone());
    }
}

void SnipOverlay::pasteInternalClipboard()
{
    if (m_internalShapes.empty())
        return;

    clearSelectionSilently();
    for (const auto &src : m_internalShapes) {
        auto copy = src->clone();
        copy->translate(QPointF(10, 10));
        copy->setSelected(true);
        m_model->addShape(copy);
        m_selectedShapes.push_back(copy);
    }
    update();
}

void SnipOverlay::beginCloneDrag(const QPoint &localPos)
{
    if (m_selectedShapes.empty())
        return;

    m_internalShapes.clear();
    const auto shapes = m_selectedShapes;
    for (const auto &src : shapes) {
        auto copy = src->clone();
        m_internalShapes.push_back(copy);
    }

    clearSelectionSilently();
    for (const auto &copy : m_internalShapes) {
        copy->setSelected(true);
        m_model->addShape(copy);
        m_selectedShapes.push_back(copy);
    }

    m_dragStart = localPos;
    m_dragOrigin = localPos;
    m_movingShape = m_selectedShapes.empty() ? nullptr : m_selectedShapes.front();
    m_state = State::Moving;
    update();
}

// ---------------------------------------------------------------- 内部工具

void SnipOverlay::clearSelectionSilently()
{
    for (const auto &s : m_selectedShapes) {
        if (s)
            s->setSelected(false);
    }
    m_selectedShapes.clear();
}
