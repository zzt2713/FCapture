#pragma once

#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QRect>

// 全屏快照：把所有屏幕拼成一张虚拟桌面大小的图（逻辑像素）
namespace ScreenCapturer {

struct Snapshot
{
    QPixmap pixmap;            // 全屏快照，尺寸 = virtualGeometry.size() * maxDpr
    QRect   virtualGeometry;   // 虚拟桌面逻辑坐标并集
    qreal   devicePixelRatio = 1.0;
    QPixmap cursorPixmap;
    QPoint  cursorPosition;      // 逻辑坐标
    QPoint  cursorHotSpot;       // 逻辑坐标
    qreal   cursorDpr = 1.0;     // 光标所在屏幕的 dpr（位图本身是物理像素）
    bool    cursorVisible = false;

    bool isValid() const { return !pixmap.isNull() && !virtualGeometry.isEmpty(); }
};

Snapshot captureAllScreens(bool includeCursor = false);

// 抓取屏幕上的逻辑矩形区域（不含鼠标指针）。
// 返回物理像素尺寸的图（devicePixelRatio 附在图上），与区域所在屏幕一致。
QImage captureRegion(const QRect &logicalRect);

// 逻辑坐标 → 快照像素坐标
inline QRect mapToSnapshot(const QRect &logical, const QRect &virtualGeometry, qreal dpr)
{
    const QRect r = logical.translated(-virtualGeometry.topLeft());
    return QRect(qRound(r.x() * dpr), qRound(r.y() * dpr),
                 qRound(r.width() * dpr), qRound(r.height() * dpr));
}

} // namespace ScreenCapturer
