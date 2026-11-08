#pragma once

#include "annotate/Annotation.h"

#include <QObject>
#include <QVector>

#include <memory>

// 标注模型：持有全部形状 + 撤销/重做命令栈
class AnnotationModel : public QObject
{
    Q_OBJECT

public:
    explicit AnnotationModel(QObject *parent = nullptr);

    void addShape(std::shared_ptr<Annotation> shape);
    void removeShape(const std::shared_ptr<Annotation> &shape);

    // 临时形状：只参与渲染，不进撤销栈。
    // 用于"橡皮擦正在绘制中"这类场景——需要和已有标注在同一图层里合成。
    void addTransientShape(const std::shared_ptr<Annotation> &shape);
    void removeTransientShape(const std::shared_ptr<Annotation> &shape);
    void undo();
    void redo();
    void clear();

    bool canUndo() const;
    bool canRedo() const;
    bool isEmpty() const;

    const QVector<std::shared_ptr<Annotation>> &shapes() const { return m_shapes; }

    // 渲染全部形状；clip 为空时不做裁剪
    void render(QPainter *p, const QRectF &clip = QRectF()) const;

    // 顶点编辑 / 命中测试（P3 用，P1 已可用）
    std::shared_ptr<Annotation> hitTestShape(const QPointF &pt, qreal tolerance = 4.0) const;

signals:
    void changed();
    void undoStateChanged(bool canUndo, bool canRedo);

private:
    void notifyChanged();

    QVector<std::shared_ptr<Annotation>> m_shapes;

    struct Op
    {
        enum class Kind { Add, Remove, Clear };
        Kind kind = Kind::Add;
        std::shared_ptr<Annotation> shape;
        QVector<std::shared_ptr<Annotation>> snapshot;   // Clear 用
    };

    QVector<Op> m_undoStack;
    QVector<Op> m_redoStack;
    static constexpr int kMaxUndoDepth = 50;
};
