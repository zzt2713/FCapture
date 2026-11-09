#include "ui/ToolIcons.h"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

namespace {

QString iconResourceName(Tool tool)
{
    switch (tool) {
    case Tool::Rectangle: return QStringLiteral("square");
    case Tool::Ellipse:   return QStringLiteral("circle");
    case Tool::Line:      return QStringLiteral("minus");
    case Tool::Arrow:     return QStringLiteral("arrow-up-right");
    case Tool::Polyline:  return QStringLiteral("spline");
    case Tool::Pencil:    return QStringLiteral("pencil");
    case Tool::Marker:    return QStringLiteral("highlighter");
    case Tool::Text:      return QStringLiteral("type");
    case Tool::Mosaic:    return QStringLiteral("grid-3x3");
    case Tool::Blur:      return QStringLiteral("circle-dashed");
    case Tool::Eraser:    return QStringLiteral("eraser");
    case Tool::Number:    return QStringLiteral("hash");
    case Tool::Magnify:   return QStringLiteral("scan-search");
    case Tool::Lasso:     return QStringLiteral("lasso-select");
    case Tool::None:      break;
    }
    return {};
}

qreal screenDpr()
{
    if (auto *screen = QGuiApplication::primaryScreen())
        return screen->devicePixelRatio();
    return 1.0;
}

// 读取 SVG 源码并把 currentColor 替换成目标颜色
QByteArray coloredSvgSource(const QString &name, const QColor &color)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QByteArray data = f.readAll();
    const QByteArray colorBytes = color.name(QColor::HexRgb).toLatin1();
    data.replace("currentColor", colorBytes);
    return data;
}

} // namespace

namespace ToolIcons {

QPixmap forTool(Tool tool, int size, const QColor &color)
{
    const QString name = iconResourceName(tool);
    if (name.isEmpty())
        return {};

    const qreal dpr = screenDpr();
    const QByteArray svg = coloredSvgSource(name, color);
    if (svg.isEmpty())
        return {};

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return {};

    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p, QRectF(0, 0, size, size));
    p.end();

    return pm;
}

// 动作图标：同样从 SVG 渲染
namespace {

QPixmap renderNamed(const QString &name, int size, const QColor &color)
{
    const qreal dpr = screenDpr();
    const QByteArray svg = coloredSvgSource(name, color);
    if (svg.isEmpty())
        return {};

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return {};

    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&p, QRectF(0, 0, size, size));
    p.end();

    return pm;
}

} // namespace

QPixmap undoIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("undo-2"), size, color); }
QPixmap redoIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("redo-2"), size, color); }
QPixmap clearIcon(int size, const QColor &color) { return renderNamed(QStringLiteral("trash-2"), size, color); }
QPixmap copyIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("copy"), size, color); }
QPixmap saveIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("save"), size, color); }
QPixmap pinIcon(int size, const QColor &color)   { return renderNamed(QStringLiteral("pin"), size, color); }
QPixmap sendIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("send"), size, color); }
QPixmap ocrIcon(int size, const QColor &color)   { return renderNamed(QStringLiteral("scan-text"), size, color); }
QPixmap barcodeIcon(int size, const QColor &color) { return renderNamed(QStringLiteral("qr-code"), size, color); }
QPixmap closeIcon(int size, const QColor &color) { return renderNamed(QStringLiteral("x"), size, color); }
QPixmap playIcon(int size, const QColor &color)  { return renderNamed(QStringLiteral("play"), size, color); }
QPixmap pauseIcon(int size, const QColor &color) { return renderNamed(QStringLiteral("pause"), size, color); }

QPixmap lineStyleIcon(int size, const QColor &color)
{
    return renderNamed(QStringLiteral("line-style"), size, color);
}

QPixmap arrowStyleIcon(int size, const QColor &color)
{
    return renderNamed(QStringLiteral("arrow-left-right"), size, color);
}

QPixmap penStyleSwatch(Qt::PenStyle style, int w, int h, const QColor &color)
{
    const qreal dpr = screenDpr();
    QPixmap pm(qRound(w * dpr), qRound(h * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 2, style, Qt::FlatCap));
    const qreal y = h / 2.0;
    p.drawLine(QPointF(2, y), QPointF(w - 2, y));
    p.end();
    return pm;
}

} // namespace ToolIcons
