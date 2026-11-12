#pragma once

#include <QWidget>

// 快捷键显示按钮 —— 移植自 Snow Shot 的 ShortcutConfigurationButton。
//
// 视觉（对照 snow 实际界面）：
//   ┌──────────────────────┐
//   │ ⊞  Ctrl+Alt+A        │   浅灰实线框 + 左侧图标 + 左对齐文字
//   └──────────────────────┘
//   录制中：主色描边 + 主色文字 + "请按下按键…"
//   冲突：  红色描边 + 红色文字
class ShortcutKeyButton : public QWidget
{
    Q_OBJECT

public:
    enum class State { Normal, Recording, Invalid };

    explicit ShortcutKeyButton(QWidget *parent = nullptr);

    void setShortcutText(const QString &text);
    QString shortcutText() const { return m_text; }

    void setState(State state);
    State state() const { return m_state; }

    QSize sizeHint() const override;
    // 布局不能把按钮压窄：压窄后文字会被 elide 成「…」（F1 显示成 …）
    QSize minimumSizeHint() const override { return sizeHint(); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void paintGridIcon(QPainter &p, const QRectF &rect, const QColor &color);

    QString m_text;
    State m_state = State::Normal;
    bool m_hover = false;
    bool m_down = false;
};
