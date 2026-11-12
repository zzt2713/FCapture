#pragma once

#include <QDialog>
#include <QPointer>
#include <QPoint>
#include <QStringList>

#include <functional>

class ShortcutConfigWidget;
class QLabel;
class ElaPushButton;

// 快捷键配置弹窗 —— 界面照 Snow Shot 的 AdModal：
//
//   ╭──────────────────────────────────────────────╮
//   │  "截图"的按键配置                        ✕  │
//   │                                              │
//   │  ┌──────────────────────┐   ┌────┐          │
//   │  │ ⊞  Ctrl+Alt+A        │   │ 🗑 │          │
//   │  └──────────────────────┘   └────┘          │
//   │                                              │
//   │  ╭ ─ ─ ─ ─ ＋ 添加按键配置 ─ ─ ─ ─ ─ ─ ─ ╮ │
//   │  ╰ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ╯ │
//   │                                              │
//   │                             取消    确定     │
//   ╰──────────────────────────────────────────────╯
class ShortcutConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShortcutConfigDialog(const QString &actionLabel,
                                  const QStringList &currentKeys,
                                  QWidget *parent = nullptr);
    ~ShortcutConfigDialog() override;

    QStringList keys() const;
    void setValidator(std::function<QString(const QString &key)> validator);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void syncAcceptEnabled();

    QPointer<ShortcutConfigWidget> m_content;
    QLabel *m_titleLabel = nullptr;
    ElaPushButton *m_rejectButton = nullptr;
    ElaPushButton *m_acceptButton = nullptr;
    QStringList m_result;
    QPoint m_dragOffset;
    bool m_dragging = false;
};
