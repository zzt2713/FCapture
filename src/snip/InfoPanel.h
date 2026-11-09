#pragma once

#include <QWidget>

// 取色/坐标信息面板（独立置顶小窗）
//
// 为什么不做成截图层自绘：PaintBar 是 SnipOverlay 的子控件，
// 子控件永远画在父窗口绘制内容之上，信息面板靠近工具栏时会被整个盖住。
// 独立窗口进 topmost 层，任何普通窗口/子控件都盖不住它。
//
// 只显示面板本身大小（约 200×90），不覆盖全屏，避免全屏合成开销。
class InfoPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InfoPanel();

    // 面板内容；color 无效时不画色块
    struct Data
    {
        QString positionText;   // (x, y)
        QString colorText;      // #RRGGBB 或 r, g, b
        QColor  swatch;
        QString copyHint;
        QString formatHint;
    };

    // 显示在 avoid 之外且不压住 avoid 的位置；avoid 为工具栏矩形（屏幕坐标）
    void showInfo(const Data &data, const QPoint &anchorGlobal, const QRect &avoidGlobal);
    // 贴在 attach 矩形的正上方、左右对齐（和放大镜组成一个整体）
    void showAbove(const Data &data, const QRect &attachGlobal);
    void hideInfo();

    QSize panelSize() const { return size(); }

    // 当前面板占据的屏幕矩形（用于避免和放大镜重叠）
    QRect globalRect() const;

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    Data m_data;
    QSize m_panelSize;
};
