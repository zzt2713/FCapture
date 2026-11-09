#pragma once

#include <QImage>
#include <QWidget>

// 放大镜（像素扫描）面板：独立置顶小窗
//
// 和 InfoPanel 同理：放大镜原本画在 SnipOverlay 上，
// 而 PaintBar 是它的子控件，放大镜靠近工具栏时会被完全盖住。
// 独立 topmost 小窗不受任何子控件影响。
class MagnifierPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MagnifierPanel();

    struct Data
    {
        QImage sample;        // 放大区域源图（已裁好）
        QColor pixelColor;    // 中心像素颜色
        bool   pixelValid = false;
        QString infoText;     // 坐标 + 颜色文字
        bool   picking = false;   // 未选好区域时画十字准心
        int    pixelSize = 8;     // 中心像素框边长
    };

    // 显示在 anchorGlobal 的右侧，并避开 avoidGlobal（信息面板/工具栏）
    void showPanel(const Data &data, const QPoint &anchorGlobal, const QRect &avoidGlobal);
    void hidePanel();

    QRect globalRect() const;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    Data m_data;
    static constexpr int kSize = 160;
};
