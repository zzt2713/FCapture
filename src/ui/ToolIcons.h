#pragma once

#include <QPixmap>
#include <Qt>

#include "annotate/AnnotationTool.h"

// 标注工具栏的图标，使用 lucide 图标集（ISC 许可）的 SVG，
// 运行时按目标颜色重新着色并栅格化到当前设备像素比。
namespace ToolIcons {

QPixmap forTool(Tool tool, int size, const QColor &color = QColor(0xE8, 0xE8, 0xE8));

QPixmap undoIcon(int size, const QColor &color);
QPixmap redoIcon(int size, const QColor &color);
QPixmap clearIcon(int size, const QColor &color);
QPixmap copyIcon(int size, const QColor &color);
QPixmap saveIcon(int size, const QColor &color);
QPixmap pinIcon(int size, const QColor &color);
QPixmap liveWindowIcon(int size, const QColor &color);
QPixmap sendIcon(int size, const QColor &color);
QPixmap ocrIcon(int size, const QColor &color);
QPixmap barcodeIcon(int size, const QColor &color);
QPixmap closeIcon(int size, const QColor &color);
QPixmap playIcon(int size, const QColor &color);
QPixmap pauseIcon(int size, const QColor &color);
QPixmap lineStyleIcon(int size, const QColor &color);
QPixmap arrowStyleIcon(int size, const QColor &color);
// 按虚线样式画一条示意线（实/虚/点/点划线），用于样式按钮
QPixmap penStyleSwatch(Qt::PenStyle style, int w, int h, const QColor &color);

} // namespace ToolIcons
