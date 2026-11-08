#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QTextEdit;

// 原地文字输入框（截图层的子控件）
//
// 作为子控件而非独立顶层窗口，原因：
//   - 独立窗口会被截图层（Qt::Tool 置顶）反复抢焦点，导致无法输入
//   - 子控件天然跟随截图层显示/销毁，不会残留桌面
//
// 外观：完全透明，字体/字号/颜色与最终标注一致，只画一圈虚线提示可编辑。
class TextInputBox : public QWidget
{
    Q_OBJECT

public:
    explicit TextInputBox(QWidget *parent);

    // 在 parent 的 localPos 处开始编辑；initial 非空时预填并全选
    void startEditing(const QPoint &localPos, const QColor &color, int fontSize,
                      const QString &initial = QString());

    QString text() const;
    QPoint  anchorPos() const { return m_anchor; }
    int     fontSize() const { return m_fontSize; }
    // 输入框实际排版宽度（0 = 未换行）。落定成标注时要带上，
    // 否则多行文本会按"不换行"重新排一次，宽度和输入时不一致。
    qreal   layoutWidth() const { return m_lastEditWidth; }

signals:
    void committed(const QString &text);
    void cancelled();
    void fontResized(int newSize);

public slots:
    void commit();
    void cancel();

protected:
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyFont();
    void relayout();
    void scheduleRelayout();

    QTextEdit *m_edit = nullptr;
    QPoint     m_anchor;          // 父控件坐标系
    int        m_fontSize = 16;

    // relayout 的幂等判断：记录上次设定值。
    // 不能用控件实际尺寸比较——setFixedWidth 设的是外部尺寸，
    // 控件实际 width() 受边框/布局影响，两者永远不等，会无限重排。
    int        m_lastEditWidth = -1;
    int        m_lastEditHeight = -1;
    int        m_lastBoxW = -1;
    int        m_lastBoxH = -1;
    bool       m_relayoutPending = false;   // 合并同一轮事件循环里的重排请求

    QColor     m_color = QColor(0xFF, 0x4A, 0x4A);
    bool       m_done = false;
    bool       m_hasFocusOnce = false;
};
