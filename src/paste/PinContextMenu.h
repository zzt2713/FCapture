#pragma once

#include "paste/PinWindow.h"

class QWidget;

// 贴图窗口的右键菜单
namespace PinContextMenu {

// 在 globalPos 处弹出针对 w 的菜单
void show(PinWindow *w, const QPoint &globalPos, QWidget *parent = nullptr);

} // namespace PinContextMenu
