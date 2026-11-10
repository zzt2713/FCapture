#pragma once

#include <QImage>
#include <QRect>
#include <QWidget>

// 长截图悬停预览：悬停右侧预览栏时，把长图对应区段盖在选区"天窗"上显示
// （天窗是 setMask 挖的洞，overlay 画不进去，只能用独立置顶小窗盖住）。
// 不抢焦点、点击穿透；悬停移开即隐藏，天窗恢复透出实时页面。
class LongShotPreview : public QWidget
{
public:
    explicit LongShotPreview(QWidget *parent = nullptr);

    // 显示 canvas 的 canvasRect 区段，缩放铺满 globalTarget（全局逻辑坐标）
    void showRegion(const QImage &canvas, const QRect &canvasRect,
                    const QRect &globalTarget);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    QImage m_region;
};
