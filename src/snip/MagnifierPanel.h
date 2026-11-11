#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

// 取色面板（逐项对齐 Snow Shot 的 ScreenshotColorPickerWindow）：
//
//   ┌──────────────────┐
//   │  11×11 像素网格   │  132×132（每格 12px），上方圆角贴合面板
//   ├──────────────────┤
//   │  X: 123 Y: 456   │  高 20，居中，13px
//   ├──────────────────┤
//   │   #FF8800        │  高 24，整行填色块，文字用反差色居中
//   └──────────────────┘
//
// 独立置顶小窗、鼠标穿透；跟随光标右下 16px，越界翻转。
class MagnifierPanel : public QWidget
{
    Q_OBJECT

public:
    explicit MagnifierPanel();

    struct Data
    {
        QImage sample;       // 中心采样的 11×11 源像素图
        QColor color;        // 中心像素颜色
        QPoint position;     // 屏幕坐标（逻辑像素），用于显示 X/Y
        bool   hexFormat = true;   // 复制格式：true=HEX，false=RGB
    };

    // 显示在光标附近（anchorGlobal 为光标全局坐标），并避开 avoidGlobal（工具栏）
    void showPanel(const Data &sample, const QPoint &anchorGlobal, const QRect &avoidGlobal);
    void hidePanel();

    QRect globalRect() const;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    Data m_data;
};
