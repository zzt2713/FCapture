#pragma once

#include "core/CommandLine.h"
#include "snip/SnipResult.h"

#include <QObject>
#include <QPointer>
#include <QSet>

class SnipOverlay;
class TrayController;
class HotkeyManager;
class SingleInstance;
class Whiteboard;
class HotCorner;
class MouseHook;
class PreferenceDialog;

class AppController : public QObject
{
    Q_OBJECT

public:
    static AppController *instance();

    void initialize();

public slots:
    void startCapture();
    void startActiveWindowCapture();
    void startPaste();

    // 命令行入口（主进程收到副进程转发时调用）
    void runCommand(const QStringList &args);
    void toggleAllImages();
    void showPreferences();
    void restartApp();
    void quitApp();

    void showGroupManager();
    void showWhiteboard(const QString &color = QString(), bool transparent = false);
    void startPickColor();
    void pasteAsImage();
    void pasteAsPlainText();

    void handleCommandLine(const QStringList &args);

signals:
    void snipCompleted(const SnipResult &result);

private:
    explicit AppController(QObject *parent = nullptr);

    void onSnipFinished(const SnipResult &result, SnipOutput output);
    void registerHotkeys();
    void restoreHotkeys();
    void syncHotkeyState();
    void captureCommon();
    // 长截图（F2 / 托盘）：截图中直接进入长截图流程，否则开新会话并记住意图
    void startLongScreenshot();

    // 命令行：截图相关的参数落到待处理的请求里
    void startCaptureWith(const CommandRequest &req);
    void applySnipRequest(SnipOverlay *overlay);
    void executeSnipOutputs(const SnipResult &result);

    TrayController  *m_tray = nullptr;
    HotkeyManager   *m_hotkeys = nullptr;
    SingleInstance  *m_singleInstance = nullptr;
    QPointer<SnipOverlay> m_overlay;
    QPointer<Whiteboard>  m_whiteboard;
    QPointer<PreferenceDialog> m_preferenceDialog;
    HotCorner *m_hotCorners = nullptr;
    MouseHook *m_mouseHook = nullptr;
    bool m_capturing = false;
    bool m_captureActiveWindow = false;
    bool m_longShotPending = false;    // 新会话创建后自动进入长截图流程
    QRect m_activeWindowRect;      // 热键触发瞬间的前台窗口（逻辑坐标）
    QSet<QString> m_hotkeyConflicts;

    // 命令行请求（截图会话期间有效）
    CommandRequest m_pendingRequest;
    bool m_hasPendingRequest = false;
};
