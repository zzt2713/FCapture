#pragma once

#include <QColor>
#include <QTextCharFormat>
#include <QVector>
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
    // 当前选区（无选中时 start == end）
    int selectionStart() const;
    int selectionEnd() const;
    bool hasSelection() const;
    // 导出当前文档里的局部样式区间（供提交时写回 TextShape）
    QVector<QTextCharFormat> exportedRuns(QVector<int> *starts, QVector<int> *ends) const;
    // 对当前选区应用/清除高亮背景
    void applyHighlightToSelection(const QColor &color);
    void applyColorToSelection(const QColor &color);
    // 载入已有文字的局部样式（编辑已存在的文字时）
    void setInitialRuns(const QVector<QTextCharFormat> &formats, const QString &text);
    QPoint  anchorPos() const { return m_anchor; }
    int     fontSize() const { return m_fontSize; }
    // 外部改字号（工具栏调粗细时同步输入框），与滚轮等效
    void    setFontSize(int px);

signals:
    void committed(const QString &text);
    void cancelled();
    void fontResized(int newSize);
    // 选区变化（用于浮动样式栏判断是否可应用局部样式）
    void selectionChanged(bool hasSelection, int start, int end);
    // 编辑结束时要带回的局部样式（按字符区间）
    void runsChanged();

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

    QColor     m_color = QColor(0xED, 0x1C, 0x24);
    bool       m_done = false;
    bool       m_hasFocusOnce = false;
};
