#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

class ShortcutKeyButton;
class QHBoxLayout;
class QLabel;

// 快捷键设置行 —— 复刻 Snow Shot 的 ShortcutKeyRow / ActionRow。
//
//   ┌──────────────────────────────────────────────────────┐
//   │  截图  ⊞                            [⊞ F1  ⓘ]       │  ← 整行是 Outline 按钮
//   └──────────────────────────────────────────────────────┘
//
// snow 的 ActionRow 本身继承 AdButton，整行就是一个可点的大按钮：
//   - 外框：圆角 cardRadius=10，1px 描边，hover 时主色
//   - 左侧：标题文字 + 小图标
//   - 右侧：快捷键按钮（ShortcutConfigurationButton）
//   - 行高：controlHeightLG(40) + paddingXXS(4) = 44
//
// 点击整行或右侧按钮都会打开配置弹窗。
class ShortcutRow : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutRow(const QString &title, const QString &action,
                         QWidget *parent = nullptr);

    // 刷新右侧按钮显示。每个键一个独立的框，横向排列；
    // 行宽放不下的键不显示（数据仍然保留在配置里）。
    void setShortcutKeys(const QStringList &keys);
    // 只把 keys 里列出的那些键标红（被别的程序占用的单键），其余保持正常
    void setInvalidKeys(const QStringList &keys);
    void setToolTipText(const QString &text);

    QString action() const { return m_action; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void rebuildKeyButtons();
    void relayoutKeyButtons();

    QString m_action;
    QString m_title;
    QStringList m_keys;
    QStringList m_invalidKeys;
    QLabel *m_titleLabel = nullptr;
    QHBoxLayout *m_keyLayout = nullptr;
    // 预先建好、按需显示：行宽变化时只改可见性，不重建控件
    QVector<ShortcutKeyButton *> m_keyButtons;
    bool m_hover = false;
    bool m_down = false;
};
