#pragma once

#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QVBoxLayout;
class ShortcutKeyButton;

// 快捷键配置内容 —— 移植自 Snow Shot 的 ShortcutKeyConfigContent
// （snow_shot/src/presentation/components/shortcutkeyrow.cpp:404-957）。
//
// 布局：
//   [⊞ Ctrl+Alt+A]                    [🗑]
//   [⊞ F1]                            [🗑]
//   ┌ ─ ─ ─ ─ ─ ＋ 添加按键配置 ─ ─ ─ ─ ─ ┐
//
// 交互（与 snow 完全一致）：
//   - 打开时若只有一个空键 → 自动进入录制
//   - 点某行键按钮 → 该行进入录制
//   - 录制时按下的键先存 pending，按钮显示该键
//   - 录制中行尾是绿色 ✓（无 pending 时禁用），点它才真正写入
//   - 非录制中行尾是红色 🗑，点它删除该行
//   - 录制中「＋ 添加」禁用
class ShortcutConfigWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutConfigWidget(const QStringList &keys, QWidget *parent = nullptr);

    void setMaxKeys(int max);
    void setValidator(std::function<QString(const QString &key)> validator);

    QStringList keys() const;
    bool isRecording() const;
    bool canAccept() const;
    void commitPending();
    void stopRecording();

    // 由外层弹窗转发按键（不依赖焦点 / grabKeyboard）
    void forwardKeyPress(QKeyEvent *e);

    static constexpr int kDefaultMaxKeys = 3;

signals:
    void stateChanged();

protected:
    void hideEvent(QHideEvent *e) override;

private:
    struct KeyConfig
    {
        QString key;
        int index = 0;
    };

    void rebuildRows();
    void startRecording(int configIndex);
    void applyPending();
    void deleteConfig(int configIndex);
    void addConfig();
    void syncAddButton();
    KeyConfig *findConfig(int configIndex);

    QVector<KeyConfig> m_configs;
    int m_nextConfigIndex = 0;
    int m_recordingIndex = -1;
    QString m_pendingKey;
    QString m_validationMessage;
    int m_maxKeys = kDefaultMaxKeys;

    QVBoxLayout *m_rowLayout = nullptr;
    QWidget *m_addButton = nullptr;
    std::function<QString(const QString &key)> m_validator;
};
