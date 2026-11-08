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
    update();
}

void SnipOverlay::clearSelection()
{
    if (m_selectedShapes.empty())
        return;
    clearSelectionSilently();
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

        // 8 个手柄统一灰白方块（不再区分角/边颜色）。
        // 角手柄同时支持"等比缩放"和"旋转"：靠近角按住不放拖动 = 旋转，
        // 直接拖 = 缩放（由 mousePress 里的判定决定，视觉上不再多加一根引线）。
        constexpr int kHandleSide = 7;
        const int count = hasRotation ? rotIndex : handles.size();

        for (int i = 0; i < count; ++i) {
            const QPointF h = handles.at(i);
            const QRectF box(h.x() - kHandleSide / 2.0, h.y() - kHandleSide / 2.0,
                             kHandleSide, kHandleSide);
            p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1.0));
            p.setBrush(QColor(0xE8, 0xE8, 0xE8));
            p.drawRect(box);
        }

        if (hasRotation) {
            // 旋转手柄：同样灰白，画成小圆点便于和方块区分；不再画引线
            const QPointF rot = handles.at(rotIndex);
            p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1.0));
            p.setBrush(QColor(0xE8, 0xE8, 0xE8));
            p.drawEllipse(rot, 4.0, 4.0);
        }
    }

    p.restore();
}

void SnipOverlay::drawSelectionBox(QPainter &p, const std::shared_ptr<Annotation> &shape)
{
    if (!shape)
        return;

    const QRectF box = shape->boundingRect();

    // 选中框：只画淡灰细线，不用主色蓝。
    // 蓝色会和选区边框抢视觉，用户会以为"选区被改了"。
    p.setPen(QPen(QColor(0xC8, 0xC8, 0xC8, 160), 1, Qt::DotLine));
    p.setBrush(Qt::NoBrush);

    if (shape->isRotatable() && !qFuzzyIsNull(shape->rotation())) {
        // 旋转后的包围盒按四角画
        const QPolygonF poly = shape->rotationTransform().map(
            QPolygonF(QRectF(shape->rawBoundingRect())));
        p.drawPolygon(poly);
    } else {
        p.drawRect(box);
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
