#pragma once

#include <QColor>
#include <QWidget>

// 贴图窗口左上角的悬浮控制按钮 —— 复刻 Snow Shot 的 OverlayControlButton。
//
// 视觉规格（对齐 snow 的 screenshotpinnedwindow.cpp + overlaycontrolbutton.h）：
//   - 32×32 圆
//   - 图标 18×18，白色
//   - 底色：半透明黑 QColor(0,0,0,115)
//   - 悬停：编辑键 → 主色；关闭键 → 错误红
//   - 按下：更深的同色
//
// 不引入 Ela：贴图层保持自绘（CLAUDE.md 硬约束）。
class PinControlButton : public QWidget
{
    Q_OBJECT

public:
    enum class Kind { Edit, Close };

    static constexpr int kSize = 32;
    static constexpr int kSpacing = 8;

    explicit PinControlButton(Kind kind, QWidget *parent = nullptr);

    void setButtonOpacity(qreal opacity);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void paintGlyph(QPainter &p, const QRectF &box, const QColor &color);

    Kind   m_kind;
    qreal  m_opacity = 1.0;
    bool   m_hover = false;
    bool   m_down = false;
};
