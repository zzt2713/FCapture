#pragma once

#include <QWidget>

// 截图左下角快捷键提示浮层（对照 Snow Shot 的 ScreenshotShortcutHintsWidget）。
//
// 关键设计：它是 SnipOverlay 的**子 QWidget**，自己 paintEvent，而不是画在
// overlay 的 paintEvent 里。这样显示/隐藏时 Qt 的 backing store 会自动把
// 该区域标脏并重绘父窗口，**不需要业务层手算脏区**——之前手算脏区漏项
// 导致的残影问题从根上消失。
//
// 鼠标穿透（WA_TransparentForMouseEvents），所以光标是否压在提示框上
// 由 SnipOverlay 传入的全局坐标判断。
class ShortcutTipsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutTipsWidget(QWidget *parent = nullptr);

    // 行内容：说明在前，chip 内是按键
    struct Row
    {
        QString keys;
        QString desc;
    };

    void setRows(const QVector<Row> &rows);
    // 按内容重新计算尺寸（几何由 SnipOverlay 定位）
    void relayout();

    // 由 overlay 每帧调用：可见性 = 有内容 && 未被遮挡
    void setObscured(bool obscured);
    bool isContentEmpty() const { return m_rows.isEmpty(); }

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    struct Metrics
    {
        int labelColumn = 0;
        int chipColumn = 0;
        int chipHeight = 0;
        int rowHeight = 22;
        int colonWidth = 0;
    };

    Metrics computeMetrics() const;

    QVector<Row> m_rows;
    Metrics m_metrics;
    bool m_obscured = false;
};
