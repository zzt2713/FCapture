#pragma once

#include <QColor>

class QWidget;

// Ela 风格取色对话框（PackBar 等标注层调用）。
// 业务层（annotate/）不直接引入 ElaWidgetTools，统一走这里。
namespace ElaColorPickerDialog {

// 弹出 Ela 取色对话框，返回用户确认的颜色；取消或关闭返回无效 QColor。
// 需要透明度时传 showAlpha = true（Ela 对话框内置 alpha 滑块）。
QColor getColor(const QColor &initial, QWidget *parent, const QString &title);
QColor getColor(const QColor &initial, QWidget *parent, const QString &title, bool showAlpha);

} // namespace ElaColorPickerDialog
