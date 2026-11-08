#pragma once

#include <QString>
#include <QUuid>
#include <QtGlobal>

// 虚拟桌面支持（Windows 10/11）
//
// 只使用公开的 IVirtualDesktopManager（CoCreateInstance 拿），
// 它能查"窗口是否在当前桌面"、能把窗口移到某个桌面 ID，
// 但不能枚举桌面。需要枚举的未公开接口 IID 每版 Windows 都变，
// 不写死 IID —— 拿不到就降级为 isSupported()==false，相关逻辑全部跳过。
namespace VirtualDesktop {

// 公开接口是否可用
bool isSupported();

// 窗口当前是否在"当前"虚拟桌面
bool isWindowOnCurrentDesktop(quintptr hwnd);

// 窗口所在桌面 ID；拿不到返回空 QUuid
QUuid windowDesktopId(quintptr hwnd);

// 当前桌面 ID；拿不到返回空 QUuid
QUuid currentDesktopId();

// 把窗口移到指定桌面；失败返回 false
bool moveWindowToDesktop(quintptr hwnd, const QUuid &desktopId);

} // namespace VirtualDesktop
