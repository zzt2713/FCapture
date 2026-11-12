#include "core/TrayController.h"
#include "core/ConfigManager.h"
#include "ui/AppMenu.h"
#include "ui/IconFactory.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QMenu>
#include <QSystemTrayIcon>

TrayController::TrayController(QObject *parent)
    : QObject(parent)
{
    m_tray = new QSystemTrayIcon(IconFactory::trayIcon(), this);

    buildMenu();

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                switch (reason) {
                case QSystemTrayIcon::Trigger:
                    emit captureRequested();
                    break;
                case QSystemTrayIcon::MiddleClick:
                    emit pasteRequested();
                    break;
                case QSystemTrayIcon::Context:
                    if (m_menu)
                        m_menu->get()->popup(QCursor::pos());
                    break;
                default:
                    break;
                }
            });

    m_tray->show();
    refreshMenus();
}

TrayController::~TrayController()
{
    // 菜单没有 QWidget 父窗口，必须自己释放
    delete m_menu;
    m_menu = nullptr;
}

void TrayController::buildMenu()
{
    m_menu = new AppMenu::Menu();

    m_captureAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::Crop,
                                             QStringLiteral("截图"));
    connect(m_captureAction, &QAction::triggered, this, &TrayController::captureRequested);

    // 长截图：框选区域后滚动拼接长图（F2 也可以进入）
    m_longShotAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::ExpandWide,
                                             QStringLiteral("长截图"));
    connect(m_longShotAction, &QAction::triggered, this,
            &TrayController::longScreenshotRequested);

    m_pasteAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::Image,
                                           QStringLiteral("贴图"));
    connect(m_pasteAction, &QAction::triggered, this, &TrayController::pasteRequested);

    m_toggleAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::Eye,
                                            QStringLiteral("隐藏所有贴图"));
    connect(m_toggleAction, &QAction::triggered, this, &TrayController::toggleImagesRequested);

    m_groupAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::ObjectGroup,
                                           QStringLiteral("分组管理…"));
    connect(m_groupAction, &QAction::triggered, this, &TrayController::groupManagerRequested);

    m_whiteboardAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::Pen,
                                                QStringLiteral("白板"));
    connect(m_whiteboardAction, &QAction::triggered, this, &TrayController::whiteboardRequested);

    m_menu->get()->addSeparator();

    m_prefAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::Gear,
                                          QStringLiteral("首选项…"));
    connect(m_prefAction, &QAction::triggered, this, &TrayController::preferencesRequested);

    m_restartAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::ArrowsRotate,
                                             QStringLiteral("重启应用"));
    connect(m_restartAction, &QAction::triggered, this, &TrayController::restartRequested);

    m_quitAction = AppMenu::addIconAction(m_menu->get(), ElaIconType::ArrowRightFromBracket,
                                          QStringLiteral("退出"));
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);
}

void TrayController::setHotkeyHint(const QString &captureHotkey)
{
    if (!m_tray)
        return;

    // 托盘悬停提示：名称 + 版本，然后逐行列出常用热键。
    // 热键都从配置动态读取，用户改过绑定后这里跟着变。
    const QString version = QCoreApplication::applicationVersion();
    const QString captureText = captureHotkey.isEmpty() ? QStringLiteral("（被占用）")
                                                        : captureHotkey;

    const auto keyText = [this](const QString &action) {
        return m_hotkeyConflicts.contains(action)
                   ? QStringLiteral("（被占用）")
                   : ConfigManager::instance()->hotkey(action);
    };

    m_tray->setToolTip(
        QStringLiteral("F-Capture %1\n截图快捷键：%2\n长截图快捷键：%3\n贴图：%4")
            .arg(version, captureText,
                 keyText(QStringLiteral("long-screenshot")),
                 keyText(QStringLiteral("paste"))));
}

void TrayController::setHotkeyConflicts(const QSet<QString> &actions)
{
    // 不能因为冲突集合没变就跳过刷新：用户改快捷键时冲突集合通常不变，
    // 但菜单上显示的键名变了，跳过后托盘菜单会一直显示旧键。
    m_hotkeyConflicts = actions;
    refreshMenus();
}

void TrayController::hideTrayIcon()
{
    if (m_tray)
        m_tray->hide();
}

void TrayController::refreshMenus()
{
    ConfigManager *cfg = ConfigManager::instance();

    const auto hotkeyText = [this, cfg](const QString &action) {
        return m_hotkeyConflicts.contains(action)
                   ? QStringLiteral("快捷键被占用")
                   : cfg->hotkey(action);
    };

    const QString captureText = hotkeyText(QStringLiteral("capture"));
    if (m_captureAction)
        m_captureAction->setText(QStringLiteral("截图\t%1").arg(captureText));

    if (m_longShotAction)
        m_longShotAction->setText(
            QStringLiteral("长截图\t%1")
                .arg(hotkeyText(QStringLiteral("long-screenshot"))));

    if (m_pasteAction)
        m_pasteAction->setText(
            QStringLiteral("贴图\t%1").arg(hotkeyText(QStringLiteral("paste"))));

    if (m_toggleAction)
        m_toggleAction->setText(
            QStringLiteral("隐藏所有贴图\t%1")
                .arg(hotkeyText(QStringLiteral("toggle-images"))));

    // 白板键默认未配置：空字符串时不显示制表符，避免菜单里出现一条空尾巴
    if (m_whiteboardAction) {
        const QString key = hotkeyText(QStringLiteral("whiteboard"));
        m_whiteboardAction->setText(
            key.isEmpty() ? QStringLiteral("白板")
                          : QStringLiteral("白板\t%1").arg(key));
    }

    setHotkeyHint(m_hotkeyConflicts.contains(QStringLiteral("capture"))
                      ? QString()
                      : cfg->hotkey(QStringLiteral("capture")));
}
