#pragma once

#include <QtGlobal>

// 全屏窗口检测：判断当前前台窗口是不是"全屏游戏"。
//
// 用途：首选项开启「全屏时屏蔽快捷键」后，热键在检测到全屏游戏时让位，
// 避免抢游戏按键。判定逻辑对齐 Snow Shot 的 focusedfullscreenwindow.cpp
// 与 gamewindowclassification.cpp：默认只看**游戏**，全屏视频/演示不屏蔽。
namespace FullscreenDetector {

// 前台是否有全屏窗口（且属于其他进程）。全屏视频/播放器也会命中。
bool focusedFullscreenWindowExists(quintptr screenHandle = 0);

// 前台是否为全屏**游戏**窗口。热键屏蔽用这个：全屏看视频时不该屏蔽。
bool focusedFullscreenGameWindowExists();

} // namespace FullscreenDetector
