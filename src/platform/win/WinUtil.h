#pragma once

#include <QString>
#include <QtGlobal>

class QRect;
class QPoint;

// Win32 封装。禁止在其他模块直接 include <windows.h>。
namespace WinUtil {

// 声明进程 DPI 感知（Per-Monitor V2），必须在 QApplication 构造前调用
void setProcessDpiAware();

// 取当前活动窗口的屏幕矩形（逻辑像素）
QRect activeWindowRect();

// 取光标下最顶层窗口的屏幕矩形（逻辑像素）
QRect windowRectAt(const QPoint &screenPos);

// 让窗口不出现在任务栏、不抢焦点（传入 QWidget::winId()）
void applyToolWindowStyle(quintptr wid);
void applyNoActivateStyle(quintptr wid);

// 显示一个 Windows 原生提示音
void beep();

// 某个功能键是否正被按住（用于"长按连续刷新"这类松手检测）。
// keyName 形如 "F5"；无法识别返回 false。
bool isFunctionKeyHeld(const QString &keyName);

// 开机自启：写/删 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// 成功返回 true；失败时 lastError() 给出原因
bool setAutoStart(bool enabled);
bool isAutoStartEnabled();
QString lastError();

} // namespace WinUtil
