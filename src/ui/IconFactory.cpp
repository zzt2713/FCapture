#include "ui/IconFactory.h"

#include <QPainter>
#include <QPainterPath>

namespace {

QPixmap makePixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    return pm;
}

QColor accent() { return QColor(0x3D, 0x8B, 0xFD); }
QColor light()  { return QColor(0xE8, 0xE8, 0xE8); }

} // namespace

namespace IconFactory {

QPixmap captureIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    QPen pen(light(), 1.6 * s, Qt::SolidLine, Qt::RoundCap);
    p.setPen(pen);
    // 四角括号
    const qreal m = 3 * s, len = 4 * s, w = size - 1;
    p.drawLine(QPointF(m, m), QPointF(m + len, m));
    p.drawLine(QPointF(m, m), QPointF(m, m + len));
    p.drawLine(QPointF(w - m, m), QPointF(w - m - len, m));
    p.drawLine(QPointF(w - m, m), QPointF(w - m, m + len));
    p.drawLine(QPointF(m, w - m), QPointF(m + len, w - m));
    p.drawLine(QPointF(m, w - m), QPointF(m, w - m - len));
    p.drawLine(QPointF(w - m, w - m), QPointF(w - m - len, w - m));
    p.drawLine(QPointF(w - m, w - m), QPointF(w - m, w - m - len));
    return pm;
}

QPixmap saveIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3 * s, 3 * s, 14 * s, 14 * s), 1.5 * s, 1.5 * s);
    p.setPen(QPen(accent(), 1.6 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(10 * s, 6 * s), QPointF(10 * s, 12 * s));
    p.drawLine(QPointF(7 * s, 9.5 * s), QPointF(10 * s, 12.5 * s));
    p.drawLine(QPointF(13 * s, 9.5 * s), QPointF(10 * s, 12.5 * s));
    return pm;
}

QPixmap copyIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(3 * s, 3 * s, 10 * s, 12 * s), 1.5 * s, 1.5 * s);
    p.setPen(QPen(accent(), 1.6 * s));
    p.drawRoundedRect(QRectF(7 * s, 6 * s, 10 * s, 12 * s), 1.5 * s, 1.5 * s);
    return pm;
}

QPixmap pinIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    // 图钉
    QPainterPath path;
    path.moveTo(7 * s, 3 * s);
    path.lineTo(13 * s, 3 * s);
    path.lineTo(12 * s, 8 * s);
    path.lineTo(15 * s, 11 * s);
    path.lineTo(5 * s, 11 * s);
    path.lineTo(8 * s, 8 * s);
    path.closeSubpath();
    p.drawPath(path);
    p.drawLine(QPointF(10 * s, 11 * s), QPointF(10 * s, 17 * s));
    return pm;
}

QPixmap closeIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.8 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(5 * s, 5 * s), QPointF(15 * s, 15 * s));
    p.drawLine(QPointF(15 * s, 5 * s), QPointF(5 * s, 15 * s));
    return pm;
}

QPixmap settingsIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10 * s, 10 * s), 3.2 * s, 3.2 * s);
    // 齿轮齿
    for (int i = 0; i < 8; ++i) {
        const qreal a = i * 45.0 * M_PI / 180.0;
        const QPointF p1(10 * s + std::cos(a) * 5.2 * s, 10 * s + std::sin(a) * 5.2 * s);
        const QPointF p2(10 * s + std::cos(a) * 7.2 * s, 10 * s + std::sin(a) * 7.2 * s);
        p.drawLine(p1, p2);
    }
    return pm;
}

QPixmap helpIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10 * s, 10 * s), 7 * s, 7 * s);
    QFont f = p.font();
    f.setPixelSize(int(10 * s));
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QStringLiteral("?"));
    return pm;
}

QPixmap exitIcon(int size)
{
    QPixmap pm = makePixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size / 20.0;
    p.setPen(QPen(light(), 1.6 * s, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(11 * s, 4 * s);
    path.lineTo(5 * s, 4 * s);
    path.lineTo(5 * s, 16 * s);
    path.lineTo(11 * s, 16 * s);
    p.drawPath(path);
    p.setPen(QPen(accent(), 1.6 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(9 * s, 10 * s), QPointF(16 * s, 10 * s));
    p.drawLine(QPointF(13.5 * s, 7.5 * s), QPointF(16 * s, 10 * s));
    p.drawLine(QPointF(13.5 * s, 12.5 * s), QPointF(16 * s, 10 * s));
    return pm;
}

QIcon appIcon()
{
    // 使用 logo/icon.png（已转成 Qt 资源 :/icon.png）
    static const QIcon icon(QStringLiteral(":/icon.png"));
    if (!icon.availableSizes().isEmpty() && !icon.isNull())
        return icon;

    // 兜底：自绘
    QIcon fallback;
    for (int s : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pm = makePixmap(s);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal k = s / 256.0;

        QLinearGradient grad(0, 0, 0, s);
        grad.setColorAt(0.0, QColor(0x4C, 0x9A, 0xFF));
        grad.setColorAt(1.0, QColor(0x2A, 0x6B, 0xDB));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawRoundedRect(QRectF(0, 0, s, s), 56 * k, 56 * k);

        QPen pen(Qt::white, 16 * k, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal m = 58 * k, len = 46 * k, w = s - m;
        p.drawLine(QPointF(m, m), QPointF(m + len, m));
        p.drawLine(QPointF(m, m), QPointF(m, m + len));
        p.drawLine(QPointF(w, m), QPointF(w - len, m));
        p.drawLine(QPointF(w, m), QPointF(w, m + len));
        p.drawLine(QPointF(m, w), QPointF(m + len, w));
        p.drawLine(QPointF(m, w), QPointF(m, w - len));
        p.drawLine(QPointF(w, w), QPointF(w - len, w));
        p.drawLine(QPointF(w, w), QPointF(w, w - len));
        fallback.addPixmap(pm);
    }
    return fallback;
}

QIcon trayIcon()
{
    // 托盘图标也用 logo，缩放到各尺寸
    static const QPixmap source(QStringLiteral(":/icon.png"));
    QIcon icon;
    if (!source.isNull()) {
        for (int s : {16, 20, 24, 32, 48, 64}) {
            icon.addPixmap(source.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        return icon;
    }

    QIcon fallback;
    for (int s : {16, 20, 24, 32, 48, 64}) {
        QPixmap pm = makePixmap(s);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal k = s / 32.0;
        QPen pen(light(), 2.4 * k, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal m = 6 * k, len = 6 * k, w = s - m;
        p.drawLine(QPointF(m, m), QPointF(m + len, m));
        p.drawLine(QPointF(m, m), QPointF(m, m + len));
        p.drawLine(QPointF(w, m), QPointF(w - len, m));
        p.drawLine(QPointF(w, m), QPointF(w, m + len));
        p.drawLine(QPointF(m, w), QPointF(m + len, w));
        p.drawLine(QPointF(m, w), QPointF(m, w - len));
        p.drawLine(QPointF(w, w), QPointF(w - len, w));
        p.drawLine(QPointF(w, w), QPointF(w, w - len));
        p.setPen(QPen(accent(), 1.6 * k));
        p.drawLine(QPointF(11 * k, 16 * k), QPointF(21 * k, 16 * k));
        p.drawLine(QPointF(16 * k, 11 * k), QPointF(16 * k, 21 * k));
        fallback.addPixmap(pm);
    }
    return fallback;
}

} // namespace IconFactory
