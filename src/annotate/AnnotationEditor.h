#pragma once

#include "annotate/AnnotationTool.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QVector>
#include <QWidget>

#include <memory>
#include <vector>

class Annotation;
class AnnotationModel;
class PaintBar;
class QKeyEvent;
class QWheelEvent;

// 编辑器的三种操作状态
enum class EditorState {
    Idle,       // 什么都没在拖
    Drawing,    // 正在画新图形
    Moving,     // 正在拖动选中图形
    Resizing,   // 正在拖锚点
    Rotating,   // 正在旋转
};

// 可复用的标注编辑画布。
//
// 抽出它是为了避免把 Whiteboard 里那套「模型 + 工具栏 + 鼠标状态机」
// 在贴图窗口再抄一遍。宿主只需要：
//   1. 创建它并铺到目标区域
//   2. paintEvent 里调 render()
//   3. 把鼠标/键盘事件转发给它的同名方法
//   4. 结束时问 isEmpty() 和 renderOnto()
//
// 它自己负责工具栏的创建、定位、撤销/重做接线。
class AnnotationEditor : public QWidget
{
    Q_OBJECT

public:
    explicit AnnotationEditor(QWidget *parent = nullptr);

    AnnotationModel *model() const { return m_model; }

    // 把当前标注渲染到 p 上（坐标系是编辑器本地坐标）
    void render(QPainter *p) const;

    // 把标注烘焙到一张图片上。
    // targetRect 是图片在编辑器坐标系里的位置，canvas 是目标图片。
    void renderOnto(QImage *canvas, const QRect &targetRect) const;

    bool isEmpty() const;

    // 当前是否有选中标注
    bool hasSelection() const { return !m_selectedShapes.empty(); }
    void clearSelection();

    // 工具栏：宿主决定何时显示，以及放在哪里。
    // takeToolbar() 把工具栏从编辑器里摘出来交给宿主（贴图场景要它显示在图片外），
    // 之后由宿主负责 show/positionNear。
    PaintBar *takeToolbar();
    void hideToolbar();
    void positionToolbar(const QRect &anchoredRect);

signals:
    void contentChanged();
    void accepted();     // 用户点了确认
    void cancelled();    // 用户点了取消 / 按了 Esc

public slots:
    void undo();
    void redo();
    void clearAll();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void beginShape(const QPoint &pos);
    void updateShape(const QPoint &pos);
    void finishShape();
    void cancelShape();
    std::shared_ptr<Annotation> createShapeForTool(Tool tool) const;
    void applyToolStyleTo(const std::shared_ptr<Annotation> &shape) const;

    // ---- 选中 / 编辑 ----
    void selectShapeAt(const QPoint &pos, bool additive);
    void updateHoveredShape(const QPoint &pos);
    bool hitTestSelectedHandle(const QPoint &pos, int *outIndex) const;
    bool hitTestRotationHandle(const QPoint &pos) const;
    bool hitTestCornerRadiusHandle(const QPoint &pos, int *outCorner) const;
    QVector<QPointF> cornerRadiusHandlePositions(const std::shared_ptr<Annotation> &shape) const;
    qreal maxCornerRadiusFor(const std::shared_ptr<Annotation> &shape) const;
    void updateCursorFor(const QPoint &pos);

    void drawSelection(QPainter &p) const;
    void drawHoverFrame(QPainter &p) const;

    AnnotationModel *m_model = nullptr;
    PaintBar *m_bar = nullptr;

    Tool m_tool = Tool::None;
    std::shared_ptr<Annotation> m_inProgress;
    QPoint m_shapeStart;
    QVector<QPointF> m_polylinePoints;
    bool m_polylineActive = false;

    // ---- 选中状态 ----
    EditorState m_state = EditorState::Idle;
    std::vector<std::shared_ptr<Annotation>> m_selectedShapes;
    std::shared_ptr<Annotation> m_hoveredShape;
    std::shared_ptr<Annotation> m_activeShape;   // 正在拖的那个
    QPoint m_dragStart;
    QPointF m_dragOrigin;                        // 拖动前的形状位置
    int m_activeHandle = -1;                     // 正在拖的锚点下标
    int m_cornerRadiusCorner = -1;               // 正在拖的圆角角标
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateStartRotation = 0.0;
};
