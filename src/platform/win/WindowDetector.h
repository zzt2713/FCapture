#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

struct WindowInfo
{
    quintptr hwnd = 0;
    QRect    logicalRect;     // 逻辑像素，屏幕坐标系
    QString  title;
    QString  className;
    int      zOrder = -1;     // 从顶到底的顺序（越小越靠顶）
};

// 窗口检测：顶层窗口枚举 + 光标命中 + 窗口栈
namespace WindowDetector {

// 截图遮罩自己的 HWND。必须登记，否则它作为全屏置顶窗口会被当成目标。
// 只排除这一个窗口：本进程的其它窗口（首选项/分组管理/白板）仍可被识别。
void setOverlayWindow(quintptr hwnd);

// 枚举所有可见顶层窗口（逻辑坐标，按 z 顺序从顶到底）
QVector<WindowInfo> enumTopLevelWindows();

// 光标位置下方的顶层窗口（逻辑坐标）；被过滤规则排除时返回空
WindowInfo windowFromPoint(const QPoint &screenPos);

// 光标下方的窗口栈（从顶到底）
QVector<WindowInfo> windowsUnderPoint(const QPoint &screenPos);

// 兼容接口：光标下方最顶层窗口的矩形
QRect bestWindowRectAt(const QPoint &screenPos);

// 光标下方窗口所属进程名（小写 exe 名），取不到返回空
QString processNameAt(const QPoint &screenPos);

} // namespace WindowDetector
