#include "snip/SnipOverlay.h"

#include "annotate/AnnotationModel.h"
#include "annotate/PaintBar.h"
#include "annotate/TextInputBox.h"
#include "annotate/shapes/FreehandShape.h"
#include "annotate/shapes/LineShape.h"
#include "annotate/shapes/MagnifyShape.h"
#include "annotate/shapes/NumberShape.h"
#include "annotate/shapes/PixelShape.h"
#include "annotate/shapes/RectShapes.h"
#include "annotate/shapes/TextShape.h"
#include "core/ConfigManager.h"

#include <QCursor>
#include <QInputDialog>
#include <QGuiApplication>
#include <QImage>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

// SnipOverlay 的标注相关实现（与 SnipOverlay.cpp 分开保持单文件可读）

void SnipOverlay::showPaintBar()
{
    if (!m_paintBar || localSelection().isEmpty())
        return;

    m_paintBarVisible = true;
    syncPaintBarPosition();
    m_paintBar->show();
    m_paintBar->raise();
    m_paintBar->setCanUndoRedo(m_model->canUndo(), m_model->canRedo());

    m_currentTool = m_paintBar->currentTool();
    update();

    // 长截图意图（F2 / 托盘进入）：选区一就绪自动进入准备态
    if (m_longShotIntent && m_longShotState == LongShotState::Off
        && !localSelection().isEmpty()) {
        m_longShotIntent = false;
        enterLongShot();
    }
}

void SnipOverlay::hidePaintBar()
{
    m_paintBarVisible = false;
    if (m_paintBar)
        m_paintBar->hide();
    if (m_state == State::Drawing)
        finishShape();
    m_currentTool = Tool::None;
    update();
}

void SnipOverlay::togglePaintBar()
{
    if (m_paintBarVisible)
        hidePaintBar();
    else
        showPaintBar();
}

void SnipOverlay::syncPaintBarPosition()
{
    if (!m_paintBar)
        return;

    const QRect sel = localSelection();
    if (sel.isEmpty()) {
        m_paintBar->hide();
        return;
    }
    m_paintBar->positionNear(sel);
}

void SnipOverlay::onToolChanged(Tool tool)
{
    m_currentTool = tool;
    m_inProgress.reset();
    m_polylinePoints.clear();
    m_lassoPoints.clear();
    if (m_state == State::Drawing || m_state == State::PolylineDrawing
        || m_state == State::LassoSelecting || m_state == State::MagnifyTarget)
        m_state = State::Adjusting;
    update();
}

void SnipOverlay::onColorChanged(const QColor &color)
{
    // 选中了标注：颜色调整直接作用于选中图形（配合"新建后自动选中"）
    for (const auto &shape : m_selectedShapes) {
        if (shape)
            shape->setColor(color);
    }
    update();
}

void SnipOverlay::onPenWidthChanged(int w)
{
    // 选中了标注：粗细/字号调整直接作用于选中图形；
    // 打开的文本输入框也同步跟随（所见即所得）
    for (const auto &shape : m_selectedShapes) {
        if (!shape)
            continue;
        shape->setPenWidth(w);
        if (shape->type() == Annotation::Type::Text) {
            if (auto *text = dynamic_cast<TextShape *>(shape.get()))
                text->setFontSize(w);
        }
    }
    if (m_textBox) {
        m_textBoxFontSize = w;
        m_textBox->setFontSize(w);
    }
    update();
}

void SnipOverlay::applyToolStyleTo(std::shared_ptr<Annotation> shape) const
{
    if (!shape)
        return;

    const ToolStyle st = ToolStyleStore::instance()->style(m_currentTool);
    shape->setColor(st.color);
    shape->setPenWidth(st.penWidth);

    switch (shape->type()) {
    case Annotation::Type::Rectangle:
    case Annotation::Type::Ellipse:
        if (auto *rect = dynamic_cast<RectShape *>(shape.get())) {
            rect->setPenStyle(st.penStyle);
            rect->setCornerRadius(shape->type() == Annotation::Type::Rectangle ? st.cornerRadius : 0);
            rect->setFilled(st.filled);
        }
        break;

    case Annotation::Type::Line:
    case Annotation::Type::Polyline:
        if (auto *line = dynamic_cast<LineShape *>(shape.get())) {
            line->setPenStyle(st.penStyle);
            line->setArrowStyle(static_cast<LineShape::ArrowStyle>(st.arrowStyle));
        }
        break;

    case Annotation::Type::Pencil:
    case Annotation::Type::Marker:
        if (auto *fh = dynamic_cast<FreehandShape *>(shape.get())) {
            fh->setPenStyle(st.penStyle);
            fh->setEllipseBrush(st.ellipseBrush);
        }
        break;

    case Annotation::Type::Mosaic:
    case Annotation::Type::Blur:
    case Annotation::Type::Eraser:
        if (auto *px = dynamic_cast<PixelShape *>(shape.get())) {
            px->setMode(st.rectMode ? PixelShape::Mode::Rect : PixelShape::Mode::Brush);
            px->setEllipseBrush(st.ellipseBrush);
        }
        break;

    case Annotation::Type::Text:
        if (auto *text = dynamic_cast<TextShape *>(shape.get())) {
            text->setFontSize(qMax(12, st.penWidth));
            text->setBackgroundColor(st.textBg);
            text->setOutline(st.textOutline);
            text->setOutlineColor(st.textOutlineColor);
            text->setOutlineWidth(st.textOutlineWidth);
        }
        break;

    default:
        break;
    }
}

std::shared_ptr<Annotation> SnipOverlay::createShapeForTool(Tool tool) const
{
    std::shared_ptr<Annotation> shape;
    switch (tool) {
    case Tool::Rectangle: shape = std::make_shared<RectShape>(RectShape::Form::Rectangle); break;
    case Tool::Ellipse:   shape = std::make_shared<RectShape>(RectShape::Form::Ellipse);   break;
    case Tool::Line:      shape = std::make_shared<LineShape>(LineShape::Kind::Line);      break;
    case Tool::Arrow:     shape = std::make_shared<LineShape>(LineShape::Kind::Arrow);     break;
    case Tool::Pencil:    shape = std::make_shared<FreehandShape>(FreehandShape::Kind::Pencil); break;
    case Tool::Marker:    shape = std::make_shared<FreehandShape>(FreehandShape::Kind::Marker); break;
    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser: {
        const auto kind = tool == Tool::Mosaic ? PixelShape::Kind::Mosaic
                          : (tool == Tool::Blur ? PixelShape::Kind::Blur
                                                : PixelShape::Kind::Eraser);
        shape = std::make_shared<PixelShape>(kind);
        break;
    }
    case Tool::Number:
    case Tool::Magnify:
        return nullptr;   // 单独处理
    default:
        return nullptr;
    }

    applyToolStyleTo(shape);
    shape->setFinished(false);
    return shape;
}

void SnipOverlay::insertCreatedShape(const std::shared_ptr<Annotation> &shape)
{
    if (!shape)
        return;

    m_model->addShape(shape);

    // 自动选中：新标注建好后立刻进入选中态，用户可以马上拖动/缩放/改样式。
    // 之前这个开关只在 insertCreatedShape 里被读，但没有任何地方把选中框画出来，
    // 表现为"开了开关也没反应"。
    const bool autoSelect = ConfigManager::instance()
                                ->value(QStringLiteral("annotate/auto_select_on_creation"), true)
                                .toBool();
    if (autoSelect) {
        clearSelectionSilently();
        shape->setSelected(true);
        m_selectedShapes.push_back(shape);
    }

    if (m_paintBar)
        m_paintBar->setCanUndoRedo(m_model->canUndo(), m_model->canRedo());
}

// ---------------------------------------------------------------- 序号

void SnipOverlay::placeNumber(const QPoint &localPos)
{
    const ToolStyle st = ToolStyleStore::instance()->style(Tool::Number);
    auto shape = std::make_shared<NumberShape>();
    shape->setColor(st.color);
    shape->setPenWidth(st.penWidth);
    shape->setGroupId(st.numberGroup);
    shape->setNumber(NumberShape::nextNumber(st.numberGroup));
    shape->setCenter(QPointF(localPos));
    shape->setFinished(true);

    insertCreatedShape(shape);
    m_state = State::Adjusting;

    update();
}

// ---------------------------------------------------------------- 放大镜

void SnipOverlay::beginMagnify(const QPoint &localPos)
{
    auto shape = std::make_shared<MagnifyShape>();
    const ToolStyle st = ToolStyleStore::instance()->style(Tool::Magnify);
    shape->setColor(st.color);
    shape->setPenWidth(qMax(1, st.penWidth));
    shape->setFinished(false);

    // 快照注入：之后拖源框、刷新后重裁都靠它
    shape->setSourceRect(QRectF(QPointF(localPos), QSizeF(0, 0)),
                         m_snapshotImage, m_snapshot.devicePixelRatio, QPointF(0, 0));

    m_inProgress = shape;
    m_magnifySourceStart = QPointF(localPos);
    m_state = State::Drawing;
    update();
}

void SnipOverlay::confirmMagnifyTarget(const QPoint &localPos)
{
    auto *mag = dynamic_cast<MagnifyShape *>(m_inProgress.get());
    if (!mag) {
        m_state = State::Adjusting;
        update();
        return;
    }

    QRectF target = QRectF(m_shapeStart, QPointF(localPos)).normalized();

    // 没拖出目标框（只是点了一下）→ 自动放到源框右侧，避免"点了没反应"
    if (target.width() < 8 || target.height() < 8) {
        const QRectF src = mag->sourceRect();
        const qreal gap = 12;
        target = QRectF(src.right() + gap, src.top(),
                        qMax<qreal>(80, src.width()), qMax<qreal>(80, src.height()));
        // 超出选区时翻到左侧
        if (target.right() > localSelection().right()) {
            target.moveRight(src.left() - gap);
            target.moveLeft(qMax<qreal>(localSelection().left(),
                                        target.right() - qMax<qreal>(80, src.width())));
        }
    }

    mag->setTargetRect(target);
    m_magnifyTargetDragging = false;

    if (!mag->hasTarget() || mag->targetRect().width() < 8
        || mag->targetRect().height() < 8) {
        m_inProgress.reset();
        m_state = State::Adjusting;
        update();
        return;
    }

    mag->setFinished(true);
    insertCreatedShape(m_inProgress);
    m_inProgress.reset();
    m_state = State::Adjusting;

    // 放大镜是一次性工具：放好一个就复位。
    // 否则工具一直挂着，用户点别处会开始画"下一个放大镜"，
    // 看起来像"放大后的框点不掉、也不消失"。
    m_currentTool = Tool::None;
    if (m_paintBar)
        m_paintBar->setTool(Tool::None);

    update();
}

// ---------------------------------------------------------------- 绘制

void SnipOverlay::beginShape(const QPoint &localPos)
{
    if (m_currentTool == Tool::Magnify) {
        beginMagnify(localPos);
        return;
    }

    m_inProgress = createShapeForTool(m_currentTool);
    if (!m_inProgress) {
        m_state = State::Adjusting;
        return;
    }

    m_state = State::Drawing;
    m_shapeStart = localPos;
    m_polylinePoints.clear();

    switch (m_currentTool) {
    case Tool::Rectangle:
    case Tool::Ellipse:
        if (auto *rect = dynamic_cast<RectShape *>(m_inProgress.get()))
            rect->setRect(QRectF(QPointF(localPos), QSizeF(0, 0)));
        break;

    case Tool::Line:
    case Tool::Arrow:
        if (auto *line = dynamic_cast<LineShape *>(m_inProgress.get()))
            line->setPoints({QPointF(localPos), QPointF(localPos)});
        break;

    case Tool::Pencil:
    case Tool::Marker:
        if (auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get()))
            fh->appendPoint(localPos);
        break;

    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser:
        if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get())) {
            if (px->mode() == PixelShape::Mode::Rect)
                px->setRect(QRectF(QPointF(localPos), QSizeF(0, 0)));
            else
                px->appendPoint(localPos);
        }
        break;

    default:
        break;
    }

    update();
}

void SnipOverlay::updateShape(const QPoint &localPos)
{
    if (!m_inProgress)
        return;

    QPoint pos = localPos;
    const bool shift = QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);

    switch (m_currentTool) {
    case Tool::Rectangle:
    case Tool::Ellipse: {
        if (shift) {
            const int side = qMax(qAbs(pos.x() - m_shapeStart.x()),
                                  qAbs(pos.y() - m_shapeStart.y()));
            const int sx = pos.x() >= m_shapeStart.x() ? 1 : -1;
            const int sy = pos.y() >= m_shapeStart.y() ? 1 : -1;
            pos = QPoint(m_shapeStart.x() + side * sx, m_shapeStart.y() + side * sy);
        }
        if (auto *rect = dynamic_cast<RectShape *>(m_inProgress.get()))
            rect->setRect(QRectF(m_shapeStart, pos).normalized());
        break;
    }

    case Tool::Line:
    case Tool::Arrow: {
        if (shift) {
            const int dx = pos.x() - m_shapeStart.x();
            const int dy = pos.y() - m_shapeStart.y();
            if (qAbs(dx) > qAbs(dy) * 2) {
                pos.setY(m_shapeStart.y());
            } else if (qAbs(dy) > qAbs(dx) * 2) {
                pos.setX(m_shapeStart.x());
            } else {
                const int d = qMax(qAbs(dx), qAbs(dy));
                pos = QPoint(m_shapeStart.x() + (dx >= 0 ? d : -d),
                             m_shapeStart.y() + (dy >= 0 ? d : -d));
            }
        }
        if (auto *line = dynamic_cast<LineShape *>(m_inProgress.get()))
            line->setLastPoint(pos);
        break;
    }

    case Tool::Pencil:
    case Tool::Marker: {
        auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get());
        if (!fh)
            break;

        // 鼠标快速移动时 Qt 会合并移动事件，直接取当前位置会在笔迹上留下
        // 大段空白（看起来就是"断裂"）。这里把上一采样点到当前位置之间的
        // 中间点按 2px 间隔补齐，保证笔迹连续。
        if (!fh->points().isEmpty()) {
            const QPointF last = fh->points().last();
            const QPointF delta = QPointF(pos) - last;
            const qreal dist = QLineF(last, QPointF(pos)).length();
            // 6px 步长足够让笔迹连续（笔宽最细 1px 也不会看出断点），
            // 又不会在快速拖动时插入大量点拖慢重绘。
            constexpr qreal kFillStep = 6.0;
            constexpr int kMaxFill = 12;
            if (dist > kFillStep) {
                const int steps = qMin(kMaxFill, int(dist / kFillStep));
                for (int i = 1; i < steps; ++i)
                    fh->appendPoint(last + delta * (qreal(i) / steps));
            }
        }
        fh->appendPoint(pos);

        if (shift) {
            QVector<QPointF> pts = fh->points();
            if (pts.size() >= 2) {
                QPointF from = pts.at(pts.size() - 2);
                QPointF to = pts.last();
                const qreal dx = to.x() - from.x();
                const qreal dy = to.y() - from.y();
                if (qAbs(dx) > qAbs(dy) * 2)
                    to.setY(from.y());
                else if (qAbs(dy) > qAbs(dx) * 2)
                    to.setX(from.x());
                pts.last() = to;
                fh->setPoints(pts);
            }
        }
        break;
    }

    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser:
        if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get())) {
            if (px->mode() == PixelShape::Mode::Rect)
                px->setRect(QRectF(m_shapeStart, pos).normalized());
            else
                px->appendPoint(pos);
        }
        break;

    case Tool::Magnify: {
        auto *mag = dynamic_cast<MagnifyShape *>(m_inProgress.get());
        if (!mag)
            break;
        if (!m_magnifyTargetDragging) {
            // 第一阶段：拖拽确定源区域。
            // 拖动中只更新框，松手时才裁片（每帧 copy 大图会掉帧）
            mag->setSourceRectPreview(QRectF(m_magnifySourceStart, QPointF(pos)).normalized());
        } else {
            // 第二阶段：拖拽确定放大后的目标区域
            mag->setTargetRect(QRectF(m_shapeStart, QPointF(pos)).normalized());
        }
        break;
    }

    default:
        break;
    }

    update();
}

void SnipOverlay::finishShape()
{
    if (!m_inProgress) {
        m_state = State::Adjusting;
        return;
    }

    // 放大镜第一阶段结束：进入第二阶段（选择放大后的目标区域）
    if (m_currentTool == Tool::Magnify) {
        auto *mag = dynamic_cast<MagnifyShape *>(m_inProgress.get());
        if (mag && mag->sourceRect().width() >= 8 && mag->sourceRect().height() >= 8) {
            // 松手才真正裁片，只做一次
            mag->captureSource();
            m_state = State::MagnifyTarget;
            m_magnifyTargetDragging = false;
            update();
            return;
        }
        m_inProgress.reset();
        m_state = State::Adjusting;
        update();
        return;
    }

    // 过滤掉太小的形状，避免误点留下痕迹
    bool keep = true;
    if (auto *rect = dynamic_cast<RectShape *>(m_inProgress.get())) {
        if (rect->rect().width() < 3 && rect->rect().height() < 3)
            keep = false;
    } else if (auto *line = dynamic_cast<LineShape *>(m_inProgress.get())) {
        const QVector<QPointF> pts = line->points();
        if (pts.size() < 2 || QLineF(pts.first(), pts.last()).length() < 3)
            keep = false;
    } else if (auto *fh = dynamic_cast<FreehandShape *>(m_inProgress.get())) {
        if (fh->points().size() < 2)
            keep = false;
    } else if (auto *px = dynamic_cast<PixelShape *>(m_inProgress.get())) {
        if (px->mode() == PixelShape::Mode::Rect) {
            const QRectF r = px->affectedRect();
            if (r.width() < 3 || r.height() < 3)
                keep = false;
        } else if (px->points().size() < 2) {
            keep = false;
        }
    }

    if (keep) {
        m_inProgress->setFinished(true);
        insertCreatedShape(m_inProgress);
    }

    m_inProgress.reset();
    m_polylinePoints.clear();
    m_state = State::Adjusting;

    update();
}

void SnipOverlay::cancelShape()
{
    m_inProgress.reset();
    m_polylinePoints.clear();
    m_state = State::Adjusting;
    update();
}

void SnipOverlay::beginTextInput(const QPoint &localPos)
{
    m_state = State::Adjusting;

    if (m_textBox) {
        m_textBox->commit();
        m_textBox = nullptr;
    }

    const ToolStyle st = ToolStyleStore::instance()->style(Tool::Text);

    TextShape *existing = nullptr;
    if (m_selectedShapes.size() == 1
        && m_selectedShapes.front()->type() == Annotation::Type::Text) {
        if (m_selectedShapes.front()->hitTest(QPointF(localPos), 2.0))
            existing = dynamic_cast<TextShape *>(m_selectedShapes.front().get());
    }

    if (!existing)
        clearSelection();

    auto *box = new TextInputBox(this);
    m_textBox = box;

    const QString initial = existing ? existing->text() : QString();
    // 输入时的字号必须就是最终标注的字号（所见即所得）。
    // 以前这里用 qMax(12, st.penWidth)，创建标注时又用 style.penWidth 再取一次，
    // 两份值可能不同 → 输入时看到的大小和落定后不一致。
    const int fontSize = existing ? existing->fontSize()
                                  : qBound(6, st.penWidth, 400);
    const QColor color = existing ? existing->color() : st.color;

    const QPoint editPos = existing ? existing->position().toPoint() : localPos;

    // 编辑期间隐藏原文字，由输入框就地显示，避免"两个框重叠"
    if (existing)
        existing->setEditing(true);

    connect(box, &TextInputBox::committed, this,
            [this, editPos, existing, fontSize](const QString &text) {
                m_textBox = nullptr;
                if (existing)
                    existing->setEditing(false);
                activateWindow();
                setFocus(Qt::OtherFocusReason);

                if (text.trimmed().isEmpty()) {
                    if (existing)
                        deleteSelected();
                    update();
                    return;
                }

                // 输入框里滚轮调过字号时，以它最后的字数为准
                const int finalSize = m_textBoxFontSize > 0 ? m_textBoxFontSize : fontSize;
                m_textBoxFontSize = 0;

                // 文本不自动换行：只有用户在标注上拖过左右侧边手柄
                // 才会设置 boxWidth，输入框宽度不回写（否则可能被意外折行）
                if (existing) {
                    existing->setText(text);
                    existing->setFontSize(finalSize);
                } else {
                    const ToolStyle style = ToolStyleStore::instance()->style(Tool::Text);
                    auto shape = std::make_shared<TextShape>();
                    shape->setColor(style.color);
                    shape->setFontSize(finalSize);
                    shape->setText(text);
                    shape->setPosition(QPointF(editPos));
                    shape->setBackgroundColor(style.textBg);
                    shape->setOutline(style.textOutline);
                    shape->setOutlineColor(style.textOutlineColor);
                    shape->setOutlineWidth(style.textOutlineWidth);
                    insertCreatedShape(shape);
                }
                update();
            });

    connect(box, &TextInputBox::cancelled, this, [this, existing]() {
        m_textBox = nullptr;
        if (existing)
            existing->setEditing(false);
        activateWindow();
        setFocus(Qt::OtherFocusReason);
        update();
    });

    connect(box, &TextInputBox::fontResized, this, [this](int newSize) {
        m_textBoxFontSize = newSize;
        ToolStyleStore::instance()->setPenWidth(Tool::Text, newSize);
    });
    m_textBoxFontSize = fontSize;

    // 编辑已有文字时输入框必须落在**文字原位置**，而不是双击点。
    // 否则双击文字会在旁边多出一个框，看起来像「又新建了一个」。
    box->startEditing(editPos, color, fontSize, initial);
}

void SnipOverlay::beginRectCornerEdit(const std::shared_ptr<Annotation> &shape)
{
    auto *rect = dynamic_cast<RectShape *>(shape.get());
    if (!rect)
        return;

    bool ok = false;
    const int radius = QInputDialog::getInt(this, QStringLiteral("圆角矩形"),
                                            QStringLiteral("圆角半径（0~50）："),
                                            qRound(rect->cornerRadius()), 0, 50, 1, &ok);
    if (ok) {
        rect->setCornerRadius(radius);
        update();
    }
}

void SnipOverlay::drawAnnotations(QPainter &p)
{
    // 橡皮擦进行中时，把这一笔也交给模型渲染（不写入撤销栈），
    // 这样它和已有标注在同一个离屏图层里正确互相作用。
    const bool eraserInProgress = m_inProgress
                                  && m_inProgress->type() == Annotation::Type::Eraser;

    if (eraserInProgress)
        m_model->addTransientShape(m_inProgress);

    if (!m_model->isEmpty())
        m_model->render(&p, QRectF(localSelection()));

    if (eraserInProgress)
        m_model->removeTransientShape(m_inProgress);
}

void SnipOverlay::drawInProgressShape(QPainter &p)
{
    if (!m_inProgress)
        return;

    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                     | QPainter::SmoothPixmapTransform, true);

    // 裁剪矩形要比选区大 2px：
    // 严格按选区裁会把画笔/箭头落在边缘那 1px 的抗锯齿半透明像素切掉，
    // 看起来就是"画出来的线有毛刺/锯齿"。
    if (!localSelection().isEmpty())
        p.setClipRect(localSelection().adjusted(-2, -2, 2, 2));

    if (m_inProgress->type() != Annotation::Type::Eraser)
        m_inProgress->paint(&p);

    p.restore();
}

void SnipOverlay::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    if (m_paintBarVisible)
        syncPaintBarPosition();
}
