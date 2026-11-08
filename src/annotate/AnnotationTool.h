#pragma once

#include <QColor>
#include <QHash>
#include <Qt>

#include <array>

// 标注工具枚举 + 每个工具独立的样式记忆
enum class Tool {
    None,
    Rectangle,
    Ellipse,
    Line,
    Arrow,
    Polyline,
    Pencil,
    Marker,
    Text,
    Mosaic,
    Blur,
    Eraser,
    Number,
    Magnify,
    Lasso
};

// 笔宽档位数量（1~5 档，实际像素值可在每档上滚轮微调）
constexpr int kPenLevels = 5;

struct ToolStyle
{
    QColor color;
    int    penWidth = 4;
    bool   rectMode = false;              // 马赛克/模糊/橡皮擦：涂抹 or 矩形选区

    Qt::PenStyle penStyle = Qt::SolidLine;
    qreal  cornerRadius = 0;              // 圆角矩形
    int    arrowStyle = 0;                // LineShape::ArrowStyle
    bool   ellipseBrush = false;          // 椭圆笔头
    bool   filled = false;                // 矩形/椭圆：是否填充实心

    QColor textBg = Qt::transparent;      // 文字背景填充
    bool   textOutline = false;
    QColor textOutlineColor = QColor(Qt::black);
    int    textOutlineWidth = 2;

    int    numberGroup = 1;               // 序号分组 1~9

    std::array<int, kPenLevels> penSizes{2, 4, 6, 10, 16};
    int    penLevel = 2;                  // 当前档位索引 0~4

    // 当前档位的实际像素值
    int levelPenWidth() const { return penSizes.at(qBound(0, penLevel, kPenLevels - 1)); }
};

class ToolStyleStore
{
public:
    static ToolStyleStore *instance();

    ToolStyle style(Tool tool) const;
    void setStyle(Tool tool, const ToolStyle &s);
    void setColor(Tool tool, const QColor &c);
    void setPenWidth(Tool tool, int w);
    void setRectMode(Tool tool, bool rect);

    // 档位：切档 = 把该工具当前笔宽设为对应档的像素值
    void setPenLevel(Tool tool, int level);
    void setLevelPenWidth(Tool tool, int level, int px);

    // 该工具是否支持矩形模式
    static bool supportsRectMode(Tool tool);
    // 该工具是否支持虚线
    static bool supportsPenStyle(Tool tool);
    // 该工具是否支持椭圆笔头
    static bool supportsEllipseBrush(Tool tool);
    static bool usesColor(Tool tool);
    static bool usesPenWidth(Tool tool);

    static QString toolName(Tool tool);

    // 持久化（QSettings）
    void load();
    void save() const;

private:
    ToolStyleStore();
    QHash<int, ToolStyle> m_styles;
};
