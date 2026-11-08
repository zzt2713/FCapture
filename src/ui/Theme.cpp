#include "ui/Theme.h"

namespace Theme {

QFont appFont()
{
    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPointSize(9);
    return f;
}

QString baseStyleSheet()
{
    // 只保留字体。颜色交给 Ela 主题（ElaThemeColor），
    // 否则 Ela 控件在 Light 主题下会出现白底白字。
    return QStringLiteral(R"(
QWidget {
    font-family: "Microsoft YaHei UI";
    font-size: 9pt;
}
)");
}

QString overlayStyleSheet()
{
    // 截图层专用：由 SnipOverlay::setStyleSheet() 挂载，只影响它自己和子控件
    // （PaintBar / SizeInputBox / TextInputBox，以及从截图层弹出的标准对话框）。
    // 这里保留 QDialog / QLineEdit / QPushButton 等规则，
    // 但绝不写 QMenu 规则 —— 菜单统一由 ElaMenu 自绘。
    return QStringLiteral(R"(
QDialog { background-color: #232323; }
QMessageBox { background-color: #232323; }

QPushButton {
    background-color: #2B2B2B;
    border: 1px solid #3D3D3D;
    border-radius: 4px;
    padding: 4px 14px;
    min-height: 22px;
    color: #E8E8E8;
}
QPushButton:hover { background-color: #3A3A3A; }
QPushButton:pressed { background-color: #333333; }
QPushButton:disabled { color: #6A6A6A; background-color: #242424; }
QPushButton:default { border-color: #3D8BFD; }

QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background-color: #262626;
    border: 1px solid #3D3D3D;
    border-radius: 4px;
    padding: 3px 6px;
    min-height: 20px;
    color: #E8E8E8;
    selection-background-color: #3D8BFD;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus,
QPlainTextEdit:focus, QTextEdit:focus { border-color: #3D8BFD; }

QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background-color: #2B2B2B;
    border: 1px solid #3D3D3D;
    color: #E8E8E8;
    selection-background-color: #3D8BFD;
}

QCheckBox, QRadioButton { spacing: 6px; color: #E8E8E8; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 14px; height: 14px;
    border: 1px solid #5A5A5A;
    border-radius: 3px;
    background-color: #262626;
}
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background-color: #3D8BFD;
    border-color: #3D8BFD;
}
QCheckBox::indicator:checked {
    image: url(:/icons/check.svg);
}

QSlider::groove:horizontal {
    height: 4px; background: #3D3D3D; border-radius: 2px;
}
QSlider::sub-page:horizontal { background: #3D8BFD; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 12px; height: 12px; margin: -5px 0;
    background: #E8E8E8; border-radius: 6px;
}

QToolTip {
    background-color: #2B2B2B;
    color: #E8E8E8;
    border: 1px solid #3D3D3D;
    padding: 4px 8px;
}

QLabel { color: #E8E8E8; background-color: transparent; }
)");
}

} // namespace Theme
