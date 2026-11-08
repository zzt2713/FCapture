#pragma once

#include "annotate/AnnotationTool.h"

#include <QColor>
#include <QImage>
#include <QWidget>

#include <memory>

class AnnotationModel;
class Annotation;
class PaintBar;

// 白板：全屏纯色（或透明）画布，复用标注系统自由手绘。
//
// 与截图层不同的是没有底图——橡皮擦在这里是"用背景色覆盖"。
class Whiteboard : public QWidget
{
    Q_OBJECT

public:
    explicit Whiteboard(const QColor &background, bool transparent, QWidget *parent = nullptr);
    ~Whiteboard() override;

    // 在当前屏幕（光标所在）铺满
    void showOnActiveScreen();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void beginShape(const QPoint &pos);
    void updateShape(const QPoint &pos);
    void finishShape();
    void cancelShape();
    void toggleToolbar();
    void syncToolbarPosition();
    void copyToClipboard();
    void saveToFile();
    QImage renderImage() const;
    std::shared_ptr<Annotation> createShapeForTool(Tool tool) const;
    void applyToolStyleTo(std::shared_ptr<Annotation> shape) const;

    AnnotationModel *m_model = nullptr;
    PaintBar *m_bar = nullptr;

    QColor m_background = Qt::white;
    bool   m_transparent = false;

    Tool   m_tool = Tool::None;
    std::shared_ptr<Annotation> m_inProgress;
    QPoint m_shapeStart;
    QVector<QPointF> m_polylinePoints;
    bool   m_polylineActive = false;
};
