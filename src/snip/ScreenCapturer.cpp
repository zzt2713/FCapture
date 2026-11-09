#include "snip/ScreenCapturer.h"

#include <QGuiApplication>
#include <QPainter>
#include <QCursor>
#include <QScreen>
#include <QVector>

#include <windows.h>

namespace {

struct GrabUnit
{
    QRect   logicalGeometry;
    QPixmap pixmap;      // 原生尺寸（物理像素）
    qreal   dpr = 1.0;
};


qreal dprAtPhysical(const POINT &pt)
{
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect geo = screen->geometry();
        if (geo.contains(pt.x, pt.y))
            return screen->devicePixelRatio();
    }
    return qGuiApp ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
}

// 把 HCURSOR 转成 QPixmap。
//
// DrawIconEx 画到 32bpp DIB 上，再用光标掩码把透明像素清成 alpha=0。
// 掩码位为 1 表示该像素不在光标形状内。
QPixmap grabCursorPixmap(HCURSOR hcursor, QPoint *hotspot)
{
    if (!hcursor)
        return {};

    ICONINFO info{};
    if (!::GetIconInfo(hcursor, &info))
        return {};

    QPixmap result;
    if (info.xHotspot || info.yHotspot || hotspot)
        *hotspot = QPoint(static_cast<int>(info.xHotspot),
                          static_cast<int>(info.yHotspot));

    BITMAP maskHeader{};
    if (info.hbmMask && ::GetObjectW(info.hbmMask, sizeof(maskHeader), &maskHeader)) {
        const int width = maskHeader.bmWidth;
        // 单色光标（无 hbmColor）时掩码高度是实际高度的两倍（AND + XOR）
        const int height = info.hbmColor ? maskHeader.bmHeight : maskHeader.bmHeight / 2;

        if (width > 0 && height > 0) {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = width;
            bi.bmiHeader.biHeight = -height;   // 负数 = 自顶向下
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;

            void *bits = nullptr;
            HDC screenDc = ::GetDC(nullptr);
            HDC memDc = ::CreateCompatibleDC(screenDc);
            HBITMAP dib = ::CreateDIBSection(screenDc, &bi, DIB_RGB_COLORS, &bits,
                                             nullptr, 0);
            if (dib && bits) {
                HGDIOBJ old = ::SelectObject(memDc, dib);
                ::PatBlt(memDc, 0, 0, width, height, BLACKNESS);
                ::DrawIconEx(memDc, 0, 0, hcursor, width, height, 0, nullptr, DI_NORMAL);
                ::GdiFlush();

                QImage image(width, height, QImage::Format_ARGB32);
                memcpy(image.bits(), bits, static_cast<size_t>(width) * height * 4);

                // 掩码位为 1 → 透明
                QImage mask(width, height, QImage::Format_Mono);
                BITMAPINFO maskInfo = bi;
                maskInfo.bmiHeader.biBitCount = 1;
                maskInfo.bmiHeader.biHeight = -height;
                if (::GetDIBits(memDc, info.hbmMask, 0, static_cast<UINT>(height),
                                mask.bits(), &maskInfo, DIB_RGB_COLORS)) {
                    for (int y = 0; y < height; ++y) {
                        const uchar *maskLine = mask.constScanLine(y);
                        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                        for (int x = 0; x < width; ++x) {
                            const bool outside =
                                ((maskLine[x >> 3] >> (7 - (x & 7))) & 1) != 0;
                            if (outside)
                                line[x] = 0x00000000;
                            else if (qAlpha(line[x]) == 0)
                                line[x] |= 0xFF000000;   // 老式光标没有 alpha 通道
                        }
                    }
                }

                result = QPixmap::fromImage(image);
                ::SelectObject(memDc, old);
                ::DeleteObject(dib);
            }
            if (memDc)
                ::DeleteDC(memDc);
            if (screenDc)
                ::ReleaseDC(nullptr, screenDc);
        }
    }

    if (info.hbmMask)
        ::DeleteObject(info.hbmMask);
    if (info.hbmColor)
        ::DeleteObject(info.hbmColor);
    return result;
}

} // namespace

namespace ScreenCapturer {

Snapshot captureAllScreens(bool includeCursor)
{
    // Qt 的 grabWindow 不抓光标，用 Win32 GetCursorInfo + DrawIconEx 画上去

    Snapshot snap;

    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return snap;

    QVector<GrabUnit> units;
    units.reserve(screens.size());

    QRect virtualGeom;
    qreal maxDpr = 1.0;

    for (QScreen *screen : screens) {
        const QPixmap pm = screen->grabWindow(0);
        if (pm.isNull())
            continue;

        GrabUnit u;
        u.logicalGeometry = screen->geometry();
        u.pixmap = pm;
        u.dpr = screen->devicePixelRatio();
        units.append(u);

        virtualGeom = virtualGeom.united(u.logicalGeometry);
        maxDpr = qMax(maxDpr, u.dpr);
    }

    if (units.isEmpty())
        return snap;

    // 所有屏幕统一按 maxDpr 缩放后拼接，保证坐标换算一致
    const QSize targetSize(qRound(virtualGeom.width() * maxDpr),
                           qRound(virtualGeom.height() * maxDpr));

    QPixmap canvas(targetSize);
    canvas.fill(Qt::black);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    for (const GrabUnit &u : units) {
        const QRect dst(
            qRound((u.logicalGeometry.x() - virtualGeom.x()) * maxDpr),
            qRound((u.logicalGeometry.y() - virtualGeom.y()) * maxDpr),
            qRound(u.logicalGeometry.width() * maxDpr),
            qRound(u.logicalGeometry.height() * maxDpr));

        if (qFuzzyCompare(u.dpr, maxDpr)) {
            p.drawPixmap(dst, u.pixmap);
        } else {
            p.drawPixmap(dst, u.pixmap, u.pixmap.rect());
        }
    }
    p.end();

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (::GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING)) {
        // 不能用 QCursor::pixmap()：Windows 11 下对系统光标经常返回空图，
        // 结果是 cursorVisible=false，「截图包含光标」和 `~` 全部静默失效。
        // 走 GetIconInfo + DrawIconEx 取真实光标位图（含热点），
        // 这也是 Win32 的标准做法，覆盖箭头 / I 型 / 手型 / 拖拽等所有系统光标。
        QPoint hotspot;
        QPixmap cursorPixmap = grabCursorPixmap(ci.hCursor, &hotspot);
        if (cursorPixmap.isNull()) {
            const QCursor fallback(Qt::ArrowCursor);
            cursorPixmap = fallback.pixmap();
            hotspot = fallback.hotSpot();
        }

        // ptScreenPos 是**物理像素**，而 Snapshot::cursorPosition / cursorHotSpot
        // 在整个项目里都按逻辑坐标使用（SnipOverlay 直接减 virtualTopLeft）。
        // 以前直接把物理坐标塞进逻辑字段，在 125%/150% 缩放下光标会被画到
        // 错误位置甚至画到屏幕外，看起来像"完全没显示"。
        const qreal cursorDpr = dprAtPhysical(ci.ptScreenPos);
        snap.cursorPixmap = cursorPixmap;
        snap.cursorPosition = QPoint(qRound(ci.ptScreenPos.x / cursorDpr),
                                     qRound(ci.ptScreenPos.y / cursorDpr));
        snap.cursorHotSpot = QPoint(qRound(hotspot.x() / cursorDpr),
                                    qRound(hotspot.y() / cursorDpr));
        snap.cursorDpr = cursorDpr;
        snap.cursorVisible = !cursorPixmap.isNull();
    }

    if (includeCursor && snap.cursorVisible) {
        // cursorPosition/cursorHotSpot 是逻辑坐标，这里要换回快照像素坐标
        const int cx = qRound((snap.cursorPosition.x() - virtualGeom.x()) * maxDpr);
        const int cy = qRound((snap.cursorPosition.y() - virtualGeom.y()) * maxDpr);
        const qreal cursorDpr = snap.cursorDpr > 0.0 ? snap.cursorDpr : maxDpr;
        const QPoint hotSpot(qRound(snap.cursorHotSpot.x() * cursorDpr),
                             qRound(snap.cursorHotSpot.y() * cursorDpr));
        QPainter cursorPaint(&canvas);
        cursorPaint.drawPixmap(QPoint(cx, cy) - hotSpot, snap.cursorPixmap);
        cursorPaint.end();
    }

    canvas.setDevicePixelRatio(maxDpr);

    snap.pixmap = canvas;
    snap.virtualGeometry = virtualGeom;
    snap.devicePixelRatio = maxDpr;
    return snap;
}

QImage captureRegion(const QRect &logicalRect)
{
    if (logicalRect.isEmpty())
        return {};

    QVector<GrabUnit> units;
    qreal maxDpr = 1.0;

    for (QScreen *screen : QGuiApplication::screens()) {
        if (!screen->geometry().intersects(logicalRect))
            continue;
        const QPixmap pm = screen->grabWindow(0);
        if (pm.isNull())
            continue;

        GrabUnit u;
        u.logicalGeometry = screen->geometry();
        u.pixmap = pm;
        u.dpr = screen->devicePixelRatio();
        units.append(u);
        maxDpr = qMax(maxDpr, u.dpr);
    }

    if (units.isEmpty())
        return {};

    // 与 captureAllScreens 相同：多屏不同 dpr 时统一按 maxDpr 缩放拼接，
    // 保证同一区域每帧的像素尺寸完全一致（长截图拼接依赖这一点）。
    QImage canvas(qRound(logicalRect.width() * maxDpr),
                  qRound(logicalRect.height() * maxDpr),
                  QImage::Format_RGB32);
    canvas.fill(Qt::black);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (const GrabUnit &u : units) {
        const QRect dst(
            qRound((u.logicalGeometry.x() - logicalRect.x()) * maxDpr),
            qRound((u.logicalGeometry.y() - logicalRect.y()) * maxDpr),
            qRound(u.logicalGeometry.width() * maxDpr),
            qRound(u.logicalGeometry.height() * maxDpr));

        if (qFuzzyCompare(u.dpr, maxDpr))
            p.drawPixmap(dst, u.pixmap);
        else
            p.drawPixmap(dst, u.pixmap, u.pixmap.rect());
    }
    p.end();

    canvas.setDevicePixelRatio(maxDpr);
    return canvas;
}

} // namespace ScreenCapturer
