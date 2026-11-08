#pragma once

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QString>

// 只用到 ElaIconType 枚举（ElaDef.h 里没有控件类），
// 业务层可以放心 include 这个头拿到图标枚举。
#include "ElaDef.h"
#include "ElaMenu.h"

class QWidget;
class QMenu;

// Ela 菜单封装。
//
// 为什么需要这一层：
//   1. CLAUDE.md 规定只有 src/ui/ 和 main.cpp 可以引入 Ela 控件头文件，
//      业务层（core / paste / snip / annotate）不直接依赖 ElaWidgetTools；
//   2. ElaMenu 的图标必须走 addElaIconAction / addMenu(ElaIconType, ...)，
//      Ela 才会用矢量字体自绘，高 DPI 下不糊。走 QMenu::addAction(QIcon)
//      是位图，100% 缩放下就明显发虚（实测）。
namespace AppMenu {

// ElaMenu 的默认 sizeHint 左右留白偏大（含 24px 图标位 + 大内边距），
// 托盘右键菜单因此显得很宽。这里只收紧宽度，绘制仍完全交给 ElaMenu。
class CompactMenu : public ElaMenu
{
public:
    explicit CompactMenu(QWidget *parent = nullptr) : ElaMenu(parent) {}

    QSize sizeHint() const override
    {
        QSize hint = ElaMenu::sizeHint();
        // 最多收到 220px；更长的菜单项（带快捷键文本）仍保留原宽度
        hint.setWidth(qMin(hint.width(), 220));
        return hint;
    }
};

// 栈上顶层菜单：析构时自动销毁。
// 这里返回的 QMenu* 实际上是 ElaMenu，业务层用下面的自由函数加项即可。
class Menu
{
public:
    explicit Menu(QWidget *parent = nullptr);
    ~Menu();

    Menu(const Menu &) = delete;
    Menu &operator=(const Menu &) = delete;

    QMenu *operator->() const { return m_menu; }
    QMenu *get() const { return m_menu; }

private:
    QMenu *m_menu = nullptr;
};

// 子菜单：父菜单接管所有权，返回值只在构建期使用
QMenu *addSubMenu(QMenu *parent, ElaIconType::IconName icon, const QString &title);
QMenu *addSubMenu(QMenu *parent, const QString &title);

// 带 Ela 矢量图标的菜单项（DPI 清晰，优先用这个）
QAction *addIconAction(QMenu *menu, ElaIconType::IconName icon, const QString &text);
QAction *addIconAction(QMenu *menu, ElaIconType::IconName icon, const QString &text,
                       const QString &shortcut);

// 给列表/树等 Model/View 用的图标。
// 这类控件只能吃 QIcon，渲染出的是位图，多倍缩放下会略糊；
// 能用 Ela 自绘控件的地方优先用 addIconAction。
QIcon icon(ElaIconType::IconName which, int pixelSize);

} // namespace AppMenu
