#include "ui/AppMenu.h"

#include "ui/ElaThemeBridge.h"

#include "ElaIcon.h"
#include "ElaMenu.h"
#include "ElaTheme.h"

#include <QMenu>

namespace {

constexpr char kElaIconProperty[] = "snipasteElaIcon";
constexpr int kMenuIconSize = 16;

QColor menuTextColor()
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::BasicText);
}

void refreshIconAction(QAction *action)
{
    if (!action)
        return;

    bool ok = false;
    const int iconValue = action->property(kElaIconProperty).toInt(&ok);
    if (!ok)
        return;

    const auto icon = static_cast<ElaIconType::IconName>(iconValue);
    action->setIcon(
        ElaIcon::getInstance()->getElaIcon(icon, kMenuIconSize, menuTextColor()));
}

void trackIconAction(QAction *action, ElaIconType::IconName icon)
{
    if (!action)
        return;

    action->setProperty(kElaIconProperty, static_cast<int>(icon));
    refreshIconAction(action);
    QObject::connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                     action, [action]() { refreshIconAction(action); });
}

} // namespace

namespace AppMenu {

Menu::Menu(QWidget *parent)
    : m_menu(new CompactMenu(parent))
{
}

Menu::~Menu()
{
    if (!m_menu)
        return;
    // 父窗口析构时会一并删除，这里先摘下父子关系避免二次删除
    m_menu->setParent(nullptr);
    delete m_menu;
    m_menu = nullptr;
}

QMenu *addSubMenu(QMenu *parent, ElaIconType::IconName icon, const QString &title)
{
    if (!parent)
        return nullptr;
    // ElaMenu::addMenu 不是虚函数，必须转成 ElaMenu 才能拿到 Ela 子菜单
    auto *ela = qobject_cast<ElaMenu *>(parent);
    if (!ela)
        return parent->addMenu(title);
    ElaMenu *subMenu = ela->addMenu(icon, title);
    trackIconAction(subMenu ? subMenu->menuAction() : nullptr, icon);
    return subMenu;
}

QMenu *addSubMenu(QMenu *parent, const QString &title)
{
    if (!parent)
        return nullptr;
    auto *ela = qobject_cast<ElaMenu *>(parent);
    if (!ela)
        return parent->addMenu(title);
    return ela->addMenu(title);
}

QAction *addIconAction(QMenu *menu, ElaIconType::IconName icon, const QString &text)
{
    auto *ela = qobject_cast<ElaMenu *>(menu);
    if (!ela)
        return menu ? menu->addAction(text) : nullptr;
    QAction *action = ela->addElaIconAction(icon, text);
    trackIconAction(action, icon);
    return action;
}

QAction *addIconAction(QMenu *menu, ElaIconType::IconName icon, const QString &text,
                       const QString &shortcut)
{
    // 保持和原来一致的「文字 + Tab + 快捷键」显示方式
    return addIconAction(menu, icon, text + QLatin1Char('\t') + shortcut);
}

QIcon icon(ElaIconType::IconName which, int pixelSize)
{
    // 不给颜色时 Ela 返回黑色图标，暗色主题下看不见
    return ElaIcon::getInstance()->getElaIcon(which, pixelSize, menuTextColor());
}

} // namespace AppMenu
