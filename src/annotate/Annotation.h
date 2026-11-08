#pragma once

#include <QColor>
#include <QJsonObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QVector>

#include <memory>

// 标注基类。所有形状用逻辑像素坐标，与选区同一坐标系。
// 坐标原点 = 截图层左上角（虚拟桌面左上角）。
//
// 旋转不修改形状自身存储的几何数据：paint() / boundingRect() / hitTest()
// 统一在基类里套一层绕中心的正/逆旋转。子类只实现 *Content / rawBoundingRect。
class Annotation
{
public:
    enum class Type {
        Rectangle,
        Ellipse,
        Line,       // 直线（可带箭头）
        Polyline,   // 折线
        Pencil,
        Marker,
        Text,
        Mosaic,
        Blur,
        Eraser,
        Number,
        Magnify
    };

    virtual ~Annotation() = default;

    virtual Type type() const = 0;

    // ---- 子类实现：均工作在"未旋转"坐标系 ----
    virtual QRectF rawBoundingRect() const = 0;
    virtual void paintContent(QPainter *p) const = 0;
    virtual bool hitTestContent(const QPointF &pt, qreal tolerance) const = 0;

    // 子类自己的控制点（矩形 8 个、直线 2 个端点、文字 1 个角点、序号 1 个中心……）
    virtual QVector<QPointF> rawHandles() const = 0;
    virtual void moveHandleRaw(int index, const QPointF &pos) = 0;

    virtual void translate(const QPointF &delta) = 0;
    // 以 center 为基准整体缩放几何数据（Shift+滚轮 / 套索缩放）
    virtual void scaleAbout(const QPointF &center, qreal factor) = 0;

    // ---- 基类统一实现 ----
    void paint(QPainter *p) const;
    QRectF boundingRect() const;
    bool hitTest(const QPointF &pt, qreal tolerance) const;

    virtual bool isRotatable() const { return true; }
    qreal rotation() const { return m_rotation; }
    // 旋转锚点永远等于形状**当前未旋转包围盒的中心**。
    //
    // 早先试过"首次旋转时锁定中心"，结果是：先旋转、再缩放（中心变了）、
    // 再旋转时支点还停在旧位置，图形会绕着一个不存在的点转，看起来就是
    // "锚点不是中心了"。正确做法是每次都用当前中心，只要几何变化时
    // 包围盒跟着更新，旋转就是绕中心进行的。
    void setRotation(qreal degrees);
    QPointF rotationAnchor() const;
    QTransform rotationTransform() const;

    // 已旋转的控制点；可旋转的形状在末尾追加一个旋转手柄
    QVector<QPointF> handlePositions() const;
    // 旋转手柄在 handlePositions() 里的下标；不可旋转返回 -1
    int rotationHandleIndex() const { return isRotatable() ? rawHandles().size() : -1; }
    // 拖动锚点。draggingHandle（可选）用于判断"哪条边该跟着鼠标走"：
    // 拖过对边后手柄语义要翻转（左边变右边），调用方每帧传入同一个索引即可。
    void moveHandle(int index, const QPointF &pos);

    virtual bool isEditable() const { return false; }
    virtual bool isDegenerate() const { return rawBoundingRect().isEmpty(); }

    virtual QJsonObject toJson() const = 0;
    virtual std::shared_ptr<Annotation> clone() const = 0;

    QColor color() const { return m_color; }
    void setColor(const QColor &c) { m_color = c; }

    int penWidth() const { return m_penWidth; }
    void setPenWidth(int w) { m_penWidth = qMax(1, w); }

    bool isFinished() const { return m_finished; }
    void setFinished(bool f) { m_finished = f; }

    bool isSelected() const { return m_selected; }
    void setSelected(bool sel) { m_selected = sel; }

    // 用于马赛克/模糊：形状改变后需要重新计算缓存
    virtual void invalidateCache() {}

protected:
    QColor m_color = QColor(0xFF, 0x4A, 0x4A);
    int    m_penWidth = 4;
    bool   m_finished = true;
    bool   m_selected = false;
    qreal  m_rotation = 0.0;

};
