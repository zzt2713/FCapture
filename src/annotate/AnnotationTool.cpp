#include "annotate/AnnotationTool.h"

#include "core/ConfigManager.h"

#include <QStringList>

namespace {

const Tool kPersistedTools[] = {
    Tool::Rectangle, Tool::Ellipse, Tool::Line, Tool::Arrow, Tool::Polyline, Tool::Pencil,
    Tool::Marker, Tool::Text, Tool::Mosaic, Tool::Blur, Tool::Eraser, Tool::Number, Tool::Magnify
};

QString toolKey(Tool tool)
{
    switch (tool) {
    case Tool::Rectangle: return QStringLiteral("Rectangle");
    case Tool::Ellipse:   return QStringLiteral("Ellipse");
    case Tool::Line:      return QStringLiteral("Line");
    case Tool::Arrow:     return QStringLiteral("Arrow");
    case Tool::Polyline:  return QStringLiteral("Polyline");
    case Tool::Pencil:    return QStringLiteral("Pencil");
    case Tool::Marker:    return QStringLiteral("Marker");
    case Tool::Text:      return QStringLiteral("Text");
    case Tool::Mosaic:    return QStringLiteral("Mosaic");
    case Tool::Blur:      return QStringLiteral("Blur");
    case Tool::Eraser:    return QStringLiteral("Eraser");
    case Tool::Number:    return QStringLiteral("Number");
    case Tool::Magnify:   return QStringLiteral("Magnify");
    default:              break;
    }
    return {};
}

} // namespace

ToolStyleStore *ToolStyleStore::instance()
{
    static ToolStyleStore s_instance;
    return &s_instance;
}

ToolStyleStore::ToolStyleStore()
{
    const QColor red(0xFF, 0x4A, 0x4A);
    const QColor yellow(0xFF, 0xD6, 0x0A);

    auto make = [&](const QColor &c, int w, int level = 2) {
        ToolStyle s;
        s.color = c;
        s.penWidth = w;
        s.penLevel = level;
        return s;
    };

    m_styles[int(Tool::Rectangle)] = make(red, 4, 1);
    m_styles[int(Tool::Ellipse)]   = make(red, 4, 1);
    m_styles[int(Tool::Line)]      = make(red, 4, 1);
    m_styles[int(Tool::Arrow)]     = make(red, 4, 1);
    m_styles[int(Tool::Polyline)]  = make(red, 4, 1);
    m_styles[int(Tool::Pencil)]    = make(red, 6, 2);
    m_styles[int(Tool::Marker)]    = make(QColor(yellow.red(), yellow.green(), yellow.blue(), 100), 12, 4);
    m_styles[int(Tool::Text)]      = make(red, 16, 4);
    m_styles[int(Tool::Mosaic)]    = make(QColor(0x3D, 0x8B, 0xFD), 12, 4);
    m_styles[int(Tool::Blur)]      = make(QColor(0x3D, 0x8B, 0xFD), 12, 4);
    m_styles[int(Tool::Eraser)]    = make(QColor(0xFF, 0xFF, 0xFF), 20, 4);
    m_styles[int(Tool::Number)]    = make(red, 4, 1);
    m_styles[int(Tool::Magnify)]   = make(red, 2, 0);
}

ToolStyle ToolStyleStore::style(Tool tool) const
{
    return m_styles.value(int(tool), ToolStyle{QColor(0xFF, 0x4A, 0x4A), 4});
}

void ToolStyleStore::setStyle(Tool tool, const ToolStyle &s)
{
    m_styles[int(tool)] = s;
}

void ToolStyleStore::setColor(Tool tool, const QColor &c)
{
    m_styles[int(tool)].color = c;
}

void ToolStyleStore::setPenWidth(Tool tool, int w)
{
    m_styles[int(tool)].penWidth = qMax(1, w);
}

void ToolStyleStore::setRectMode(Tool tool, bool rect)
{
    m_styles[int(tool)].rectMode = rect;
}

void ToolStyleStore::setPenLevel(Tool tool, int level)
{
    level = qBound(0, level, kPenLevels - 1);
    ToolStyle s = m_styles[int(tool)];
    s.penLevel = level;
    s.penWidth = s.penSizes.at(level);
    m_styles[int(tool)] = s;
}

void ToolStyleStore::setLevelPenWidth(Tool tool, int level, int px)
{
    if (level < 0 || level >= kPenLevels)
        return;
    ToolStyle s = m_styles[int(tool)];
    s.penSizes.at(level) = qBound(1, px, 400);
    if (s.penLevel == level)
        s.penWidth = s.penSizes.at(level);
    m_styles[int(tool)] = s;
}

bool ToolStyleStore::supportsRectMode(Tool tool)
{
    return tool == Tool::Mosaic || tool == Tool::Blur || tool == Tool::Eraser;
}

bool ToolStyleStore::supportsPenStyle(Tool tool)
{
    switch (tool) {
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Line:
    case Tool::Arrow:
    case Tool::Polyline:
    case Tool::Pencil:
        return true;
    default:
        return false;
    }
}

bool ToolStyleStore::supportsEllipseBrush(Tool tool)
{
    return tool == Tool::Pencil || tool == Tool::Marker
           || tool == Tool::Mosaic || tool == Tool::Blur || tool == Tool::Eraser;
}

bool ToolStyleStore::usesColor(Tool tool)
{
    switch (tool) {
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Line:
    case Tool::Arrow:
    case Tool::Polyline:
    case Tool::Pencil:
    case Tool::Marker:
    case Tool::Text:
    case Tool::Number:
    case Tool::Magnify:
        return true;
    default:
        return false;
    }
}

bool ToolStyleStore::usesPenWidth(Tool tool)
{
    return tool != Tool::None && tool != Tool::Lasso;
}

QString ToolStyleStore::toolName(Tool tool)
{
    switch (tool) {
    case Tool::Rectangle: return QStringLiteral("矩形");
    case Tool::Ellipse:   return QStringLiteral("椭圆");
    case Tool::Line:      return QStringLiteral("直线");
    case Tool::Arrow:     return QStringLiteral("箭头");
    case Tool::Polyline:  return QStringLiteral("折线");
    case Tool::Pencil:    return QStringLiteral("铅笔");
    case Tool::Marker:    return QStringLiteral("马克笔");
    case Tool::Text:      return QStringLiteral("文字");
    case Tool::Mosaic:    return QStringLiteral("马赛克");
    case Tool::Blur:      return QStringLiteral("模糊");
    case Tool::Eraser:    return QStringLiteral("橡皮擦");
    case Tool::Number:    return QStringLiteral("序号");
    case Tool::Magnify:   return QStringLiteral("放大镜");
    case Tool::Lasso:     return QStringLiteral("套索");
    case Tool::None:      break;
    }
    return QStringLiteral("无");
}

// ---------------------------------------------------------------- 持久化

void ToolStyleStore::load()
{
    for (Tool tool : kPersistedTools) {
        const QString raw = ConfigManager::instance()
                                ->value(QStringLiteral("annotate/toolStyles/") + toolKey(tool))
                                .toString();
        if (raw.isEmpty())
            continue;

        // 格式：颜色,笔宽,档位,档0|档1|档2|档3|档4,虚线,圆角,箭头,椭圆笔头,实心
        const QStringList parts = raw.split(QLatin1Char(','));
        if (parts.size() < 2)
            continue;

        ToolStyle s = m_styles[int(tool)];
        const QColor c(parts.at(0));
        if (c.isValid())
            s.color = c;
        s.penWidth = qMax(1, parts.at(1).toInt());

        if (parts.size() > 2)
            s.penLevel = qBound(0, parts.at(2).toInt(), kPenLevels - 1);
        if (parts.size() > 3) {
            const QStringList sizes = parts.at(3).split(QLatin1Char('|'));
            for (int i = 0; i < kPenLevels && i < sizes.size(); ++i)
                s.penSizes.at(i) = qBound(1, sizes.at(i).toInt(), 400);
        }
        if (parts.size() > 4)
            s.penStyle = Qt::PenStyle(parts.at(4).toInt());
        if (parts.size() > 5)
            s.cornerRadius = qMax<qreal>(0, parts.at(5).toDouble());
        if (parts.size() > 6)
            s.arrowStyle = parts.at(6).toInt();
        if (parts.size() > 7)
            s.ellipseBrush = parts.at(7).toInt() != 0;
        if (parts.size() > 8)
            s.filled = parts.at(8).toInt() != 0;

        m_styles[int(tool)] = s;
    }
}

void ToolStyleStore::save() const
{
    for (Tool tool : kPersistedTools) {
        const ToolStyle s = style(tool);
        QStringList sizes;
        for (int px : s.penSizes)
            sizes << QString::number(px);

        const QStringList parts = {
            s.color.name(QColor::HexArgb),
            QString::number(s.penWidth),
            QString::number(s.penLevel),
            sizes.join(QLatin1Char('|')),
            QString::number(int(s.penStyle)),
            QString::number(s.cornerRadius),
            QString::number(s.arrowStyle),
            QString::number(s.ellipseBrush ? 1 : 0),
            QString::number(s.filled ? 1 : 0)
        };
        ConfigManager::instance()->setValue(QStringLiteral("annotate/toolStyles/") + toolKey(tool),
                                            parts.join(QLatin1Char(',')));
    }
}
