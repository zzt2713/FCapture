#pragma once

#include "ElaDialog.h"

#include "core/UpdateChecker.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class ElaComboBox;
class ElaLineEdit;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;
class NavList;
class ElaPushButton;
class ShortcutRow;

class QHideEvent;
class QShowEvent;
class QStackedWidget;
class QVBoxLayout;

// 首选项：左侧导航 + 右侧内容堆叠。
//
// 不用 ElaTabWidget / ElaScrollArea：前者不做页面堆叠（内容会重叠），
// 后者的 sizeHint 会把内容撑出视口。布局壳自己写，Ela 只提供控件外观。
// 改动即时写入 config.ini（不需要"应用"按钮）
class PreferenceDialog : public ElaDialog
{
    Q_OBJECT

public:
    explicit PreferenceDialog(QWidget *parent = nullptr);
    ~PreferenceDialog() override;

    void setHotkeyConflicts(const QSet<QString> &actions);
    // 按单个键标记冲突：action -> 注册失败的键（被其他程序占用/无法解析）
    void setHotkeyFailedKeys(const QHash<QString, QStringList> &failedKeys);
    void showHotkeyResult(const QString &action, bool success,
                          const QString &message = QString());

signals:
    void hotkeyRecordingChanged(bool recording);
    void hotkeyChangeRequested(const QString &action, const QString &hotkey);
    // 多快捷键：一次给完整列表（snow 的按键配置弹窗支持多个备用键）
    void hotkeysChangeRequested(const QString &action, const QStringList &hotkeys);
    // 托盘菜单里的「重启应用」已移到「关于」页，由 AppController 执行
    void restartRequested();

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // 构建 10 个设置页，顺序与左侧导航一致
    void buildPages();

    QWidget *buildGeneralPage();
    QWidget *buildSnipPage();
    QWidget *buildControlPage();
    QWidget *buildPastePage();
    QWidget *buildAnnotatePage();
    QWidget *buildInterfacePage();
    QWidget *buildOutputPage();
    QWidget *buildHistoryPage();
    QWidget *buildHelpPage();
    QWidget *buildAboutPage();

    // 历史页：缩略图网格自绘（ElaListView 在 IconMode 下不显示模型里的缩略图）
    void refreshHistory();
    // 关于页：查询 GitHub Releases 并比较版本
    void checkForUpdates();
    // 关于页：下载并安装待处理的更新
    void startUpdateDownload();

    QWidget *m_historyList = nullptr;
    ElaText *m_updateStatus = nullptr;
    ElaPushButton *m_updateInstallBtn = nullptr;
    UpdateChecker *m_updateChecker = nullptr;
    UpdateChecker::Release m_pendingRelease;
    ElaText *m_historyHint = nullptr;
    int m_historyPageIndex = -1;

    // 一行「说明文字 + 开关」，开关直接绑定到配置项
    QWidget *toggleRow(QWidget *parent, const QString &text, const QString &key);

    // 把一个 bool 配置项绑到开关上
    void bindToggle(ElaToggleSwitch *sw, const QString &key);
    // 把一个 string 配置项绑到 lineEdit 上
    void bindString(ElaLineEdit *edit, const QString &key);
    // 把一个 int 配置项绑到 spinBox 上
    void bindInt(ElaSpinBox *box, const QString &key);

    // 按冲突状态刷新快捷键按钮（文字 + 红色高亮）
    void applyHotkeyButtonState(const QString &action);

    NavList *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QHash<QString, ShortcutRow *> m_hotkeyRows;
    QSet<QString> m_conflictedActions;
    QHash<QString, QStringList> m_failedKeys;
};
