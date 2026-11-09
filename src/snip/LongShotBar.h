#pragma once

#include <QWidget>

// 长截图工具栏（独立置顶小窗，与正常截图工具栏完全分开）：
// 开始 / 暂停 / 保存 / 复制 / 钉（贴到屏幕）/ 关闭 六个图标键，
// 图标与 PaintBar 同用 ToolIcons（lucide），悬停有自绘提示。
// 不抢焦点（滚轮留给下层页面）。
class LongShotBar : public QWidget
{
    Q_OBJECT

public:
    explicit LongShotBar();

    // 状态同步：捕获中 开始 置灰、暂停 可用；有拼接内容后 保存/复制/钉 才可用
    void setRunning(bool running);
    void setHasContent(bool hasContent);

    // 定位到选区下方/上方（全局逻辑坐标），不压住捕获区域
    void positionNear(const QRect &selectionGlobalRect);

signals:
    void startRequested();
    void pauseRequested();
    void saveRequested();
    void copyRequested();
    void pinRequested();
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    enum ButtonId { BtnStart, BtnPause, BtnSave, BtnCopy, BtnPin, BtnClose, BtnCount };

    void rebuildLayout();
    int hitButton(const QPoint &pos) const;
    bool buttonEnabled(int id) const;

    struct Item
    {
        QRect rect;
    };
    Item m_items[BtnCount];
    int  m_hover = -1;
    int  m_pressed = -1;
    bool m_running = false;
    bool m_hasContent = false;

    static constexpr int kPadding = 6;
    static constexpr int kButtonSize = 28;
    static constexpr int kGap = 2;
    static constexpr int kSeparatorGap = 9;
    static constexpr int kTooltipArea = 24;
};
