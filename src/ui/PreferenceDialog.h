#pragma once

#include "ElaDialog.h"

#include "core/UpdateChecker.h"

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>

class ElaComboBox;
class ElaLineEdit;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;
class HotkeyRecorder;
class NavList;
class ElaPushButton;

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
    void showHotkeyResult(const QString &action, bool success,
                          const QString &message = QString());

signals:
    void hotkeyRecordingChanged(bool recording);
    void hotkeyChangeRequested(const QString &action, const QString &hotkey);
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

    NavList *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QPointer<HotkeyRecorder> m_recordingRecorder;
    QHash<QString, HotkeyRecorder *> m_hotkeyRecorders;
    QHash<QString, ElaText *> m_hotkeyHints;
    QSet<QString> m_conflictedActions;
};
