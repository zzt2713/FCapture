#pragma once

#include <QColor>
#include <QFont>
#include <QString>

namespace Theme {

// 色板（复刻 Snipaste 极简暗色）
inline const QColor kBg            = QColor(0x1E, 0x1E, 0x1E);
inline const QColor kToolbarBg     = QColor(0x2B, 0x2B, 0x2B);
inline const QColor kToolbarBorder = QColor(0x3D, 0x3D, 0x3D);
inline const QColor kText          = QColor(0xE8, 0xE8, 0xE8);
inline const QColor kTextDim       = QColor(0x8A, 0x8A, 0x8A);
inline const QColor kAccent        = QColor(0x3D, 0x8B, 0xFD);
inline const QColor kHover         = QColor(0x3A, 0x3A, 0x3A);
inline const QColor kMask          = QColor(0, 0, 0, 165);
inline const QColor kGuideLine     = QColor(0xFF, 0x4D, 0x4D);

// 全局 QSS：只设字体，不设任何颜色。
// Ela 控件继承自 QWidget，一旦被 QSS 刷上固定颜色就会破坏主题（Light 下白底白字）。
QString baseStyleSheet();

// 截图层 QSS：只对 SnipOverlay 及其子控件生效（自绘控件专用）。
// 由 SnipOverlay 构造函数 setStyleSheet() 挂载，不污染应用级 Ela 界面。
QString overlayStyleSheet();

QFont appFont();

} // namespace Theme
