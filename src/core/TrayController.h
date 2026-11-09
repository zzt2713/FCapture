#pragma once

#include <QObject>
#include <QSet>
#include <QString>

class QSystemTrayIcon;
class QAction;
class QMenu;

namespace AppMenu {
class Menu;
} // namespace AppMenu

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    void setHotkeyHint(const QString &captureHotkey);
    void setHotkeyConflicts(const QSet<QString> &actions);
    void hideTrayIcon();
    void refreshMenus();

signals:
    void captureRequested();
    void longScreenshotRequested();
    void pasteRequested();
    void toggleImagesRequested();
    void preferencesRequested();
    void groupManagerRequested();
    void whiteboardRequested();
    void restartRequested();
    void quitRequested();

private:
    void buildMenu();

    QSystemTrayIcon *m_tray = nullptr;
    AppMenu::Menu *m_menu = nullptr;

    QAction *m_captureAction = nullptr;
    QAction *m_longShotAction = nullptr;
    QAction *m_pasteAction = nullptr;
    QAction *m_toggleAction = nullptr;
    QAction *m_prefAction = nullptr;
    QAction *m_groupAction = nullptr;
    QAction *m_whiteboardAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_quitAction = nullptr;
    QSet<QString> m_hotkeyConflicts;
};
