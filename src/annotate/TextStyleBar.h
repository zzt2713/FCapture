#pragma once

#include <QColor>
#include <QWidget>

class TextShape;

// 文字标注的浮动样式工具栏（参考 Snow Shot 的 quick text toolbar）。
//
// 选中一个文字标注时，浮在它正上方，可快速调整：颜色、字号、背景、描边。
// 自绘控件（与 PaintBar 同一套视觉），不抢焦点。
class TextStyleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TextStyleBar(QWidget *parent = nullptr);

    // 绑定当前编辑的文字标注（nullptr = 隐藏）
    void setTarget(TextShape *shape);
    // 编辑中输入框有选区：按钮作用于选中文字（局部样式）
    void setEditingSelection(int start, int end, bool active);
    bool isEditingSelection() const { return m_editSelActive; }
    // 贴到目标上方（父控件坐标系）
    void positionAbove(const QRect &targetLocalRect);

signals:
    // 样式已改动，宿主需要重绘并同步到样式存储
    void styleChanged();
    // 编辑中要求给选区应用高亮/前景色
    void selectionHighlightRequested(const QColor &color);
    void selectionColorRequested(const QColor &color);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    enum class Btn { Color, FontSmaller, FontLarger, Bg, Outline, Highlight, Count };

    QRect buttonRect(Btn b) const;
    Btn   buttonAt(const QPoint &pos) const;
    void  applyFontDelta(int delta);
    void  pickColor();
    void  toggleBackground();
    void  toggleOutline();

    TextShape *m_target = nullptr;
    bool m_editSelActive = false;
    int  m_editSelStart = 0;
    int  m_editSelEnd = 0;
    int  m_hover = -1;
    int  m_pressed = -1;

    // 视觉常量（与 PaintBar 的浅色主题一致）
    static constexpr int kPadding = 5;
    static constexpr int kBtnSize = 24;
    static constexpr int kGap = 3;
    static constexpr int kSwatchSize = 16;
};
