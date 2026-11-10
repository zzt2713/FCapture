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

// 推到置顶带最前：任务栏/桌面宠物这类 WS_EX_TOPMOST 窗口和遮罩同在置顶带，
// Qt 的 raise() 压不住它们，必须显式 SetWindowPos(HWND_TOPMOST)
void forceTopmost(quintptr wid);

// Z 序上方是否有"外进程"窗口压着（本进程自己的浮窗不算）。
// 置顶守卫用它决定要不要推一把——无谓的 SetWindowPos 会让 DWM 反复重排
bool isForeignWindowAbove(quintptr wid);

// 鼠标穿透开关（WS_EX_TRANSPARENT）：开启后滚轮/点击直接落到下层窗口，
// 长截图滚动时用它把输入"放行"给被截的应用（传入 QWidget::winId()）
void setClickThrough(quintptr wid, bool on);

// 把焦点交给光标下的顶层窗口：滚轮消息默认发给焦点窗口，
// 截图层不交出焦点的话长截图滚动页面会没反应
void activateWindowAt(const QPoint &logicalPos);

// 把滚轮转发给光标下的窗口（selfWid 自己及其子窗口会被跳过防回环）。
// 鼠标穿透失效时的兜底，保证滚轮一定能滚页面。
void forwardWheel(quintptr selfWid, int delta, const QPoint &logicalPos);

// 显示一个 Windows 原生提示音
void beep();

// 裁剪工作集：把空闲物理页立即还给系统（任务管理器内存列回落用）。
// 只影响驻留页，不影响堆上的实际占用。
void trimWorkingSet();

// 某个功能键是否正被按住（用于"长按连续刷新"这类松手检测）。
// keyName 形如 "F5"；无法识别返回 false。
bool isFunctionKeyHeld(const QString &keyName);

// 开机自启：写/删 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// 成功返回 true；失败时 lastError() 给出原因
bool setAutoStart(bool enabled);
bool isAutoStartEnabled();
QString lastError();

} // namespace WinUtil
