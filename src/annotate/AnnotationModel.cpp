#include "annotate/AnnotationModel.h"

#include <QPainter>

#include <algorithm>

AnnotationModel::AnnotationModel(QObject *parent)
    : QObject(parent)
{
}

void AnnotationModel::addShape(std::shared_ptr<Annotation> shape)
{
    if (!shape)
        return;

    m_shapes.append(shape);

    Op op;
    op.kind = Op::Kind::Add;
    op.shape = shape;
    m_undoStack.append(op);
    while (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.removeFirst();

    m_redoStack.clear();
    notifyChanged();
}

void AnnotationModel::removeShape(const std::shared_ptr<Annotation> &shape)
{
    const int idx = m_shapes.indexOf(shape);
    if (idx < 0)
        return;

    m_shapes.removeAt(idx);

    Op op;
    op.kind = Op::Kind::Remove;
    op.shape = shape;
    m_undoStack.append(op);
    while (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.removeFirst();

    m_redoStack.clear();
    notifyChanged();
}

void AnnotationModel::addTransientShape(const std::shared_ptr<Annotation> &shape)
{
    if (!shape)
        return;
    m_shapes.append(shape);   // 不进撤销栈，不发 changed 信号
}

void AnnotationModel::removeTransientShape(const std::shared_ptr<Annotation> &shape)
{
    m_shapes.removeAll(shape);
}

void AnnotationModel::undo()
{
    if (m_undoStack.isEmpty())
        return;

    const Op op = m_undoStack.takeLast();

    switch (op.kind) {
    case Op::Kind::Add:
        m_shapes.removeAll(op.shape);
        break;
    case Op::Kind::Remove:
        m_shapes.append(op.shape);
        break;
    case Op::Kind::Clear:
        m_shapes = op.snapshot;
        break;
    }

    m_redoStack.append(op);
    notifyChanged();
}

void AnnotationModel::redo()
{
    if (m_redoStack.isEmpty())
        return;

    const Op op = m_redoStack.takeLast();

    switch (op.kind) {
    case Op::Kind::Add:
        m_shapes.append(op.shape);
        break;
    case Op::Kind::Remove:
        m_shapes.removeAll(op.shape);
        break;
    case Op::Kind::Clear:
        m_shapes.clear();
        break;
    }

    m_undoStack.append(op);
    notifyChanged();
}

void AnnotationModel::clear()
{
    if (m_shapes.isEmpty())
        return;

    Op op;
    op.kind = Op::Kind::Clear;
    op.snapshot = m_shapes;
    m_undoStack.append(op);
    while (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.removeFirst();

    m_shapes.clear();
    m_redoStack.clear();
    notifyChanged();
}

bool AnnotationModel::canUndo() const
{
    return !m_undoStack.isEmpty();
}

bool AnnotationModel::canRedo() const
{
    return !m_redoStack.isEmpty();
}

bool AnnotationModel::isEmpty() const
{
    return m_shapes.isEmpty();
}

void AnnotationModel::render(QPainter *p, const QRectF &clip) const
{
    if (!p)
        return;

    p->save();

    // 标注必须抗锯齿，否则线条和圆弧会有明显锯齿。
    // 截图层整体是关掉抗锯齿的（为了让选区边框保持像素精确），
    // 所以这里要显式打开。
    p->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                      | QPainter::SmoothPixmapTransform, true);

    if (!clip.isEmpty())
        p->setClipRect(clip);

    // 有橡皮擦时，标注必须先画到独立的透明层上，橡皮擦在那一层上挖空，
    // 最后把整层叠到目标画布。这样橡皮擦只擦掉"自己画的标注"，底图不受影响。
    const bool hasEraser = std::any_of(m_shapes.cbegin(), m_shapes.cend(),
                                       [](const std::shared_ptr<Annotation> &s) {
                                           return s && s->type() == Annotation::Type::Eraser;
                                       });

    if (!hasEraser) {
        for (const auto &shape : m_shapes) {
            if (!shape)
                continue;
            p->save();
            shape->paint(p);
            p->restore();
        }
        p->restore();
        return;
    }

    // ---- 有橡皮擦：走离屏图层合成 ----
    const QRect layerRect = clip.isEmpty() ? QRectF(p->clipBoundingRect()).toRect()
                                           : clip.toRect();
    if (layerRect.isEmpty()) {
        p->restore();
        return;
    }

    QImage layer(layerRect.size() * p->device()->devicePixelRatio(),
                 QImage::Format_ARGB32_Premultiplied);
    layer.setDevicePixelRatio(p->device()->devicePixelRatio());
    layer.fill(Qt::transparent);

    {
        QPainter lp(&layer);
        lp.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                          | QPainter::SmoothPixmapTransform, true);
        lp.translate(-layerRect.topLeft());

        for (const auto &shape : m_shapes) {
            if (!shape)
                continue;
            lp.save();
            shape->paint(&lp);
            lp.restore();
        }
    }

    p->drawImage(layerRect.topLeft(), layer);
    p->restore();
}

std::shared_ptr<Annotation> AnnotationModel::hitTestShape(const QPointF &pt, qreal tolerance) const
{
    // 后画的在上层，倒序查找
    for (int i = m_shapes.size() - 1; i >= 0; --i) {
        if (m_shapes.at(i) && m_shapes.at(i)->hitTest(pt, tolerance))
            return m_shapes.at(i);
    }
    return nullptr;
}

void AnnotationModel::notifyChanged()
{
    emit changed();
    emit undoStateChanged(canUndo(), canRedo());
}
