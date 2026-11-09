#include "ui/ElaColorPickerDialog.h"

#include "ElaColorDialog.h"

#include <QCursor>
#include <QDialog>
#include <QGuiApplication>
#include <QScreen>

namespace {

// 注意：不能让这个对话框以截图层为父窗口。
// SnipOverlay 挂了一份写死暗色的 overlayStyleSheet（QDialog / QLineEdit 等），
// Qt 的样式表会沿父子链下发给子对话框，把 Ela 自绘的浅色/深色配色整个盖掉，
// 表现为"取色器颜色不对、字看不清"。用无父窗口 + 置顶代替父子关系。
QColor runDialog(const QColor &initial, const QString &title, QWidget *anchor)
{
    ElaColorDialog dialog(nullptr);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);

    if (!title.isEmpty())
        dialog.setWindowTitle(title);
    if (initial.isValid())
        dialog.setCurrentColor(initial);

    // 贴到触发控件所在屏幕上居中，避免出现在主屏而不是当前屏
    QScreen *screen = anchor ? QGuiApplication::screenAt(anchor->mapToGlobal(anchor->rect().center()))
                             : QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect avail = screen->availableGeometry();
        dialog.adjustSize();
        dialog.move(avail.center().x() - dialog.width() / 2,
                    avail.center().y() - dialog.height() / 2);
    }

    // Ela 的"确定/取消"两个按钮都只关闭对话框，**不区分 Accepted/Rejected**
    // （实测 exec() 恒返回 0）。所以不能用返回值判断，改用两个信号区分：
    //   colorSelected  → 只有取色器/色板真的产生颜色时才发（含"确定"时）
    //   普通关闭/取消    → 不发信号
    QColor picked;
    QObject::connect(&dialog, &ElaColorDialog::colorSelected,
                     [&picked](const QColor &color) { picked = color; });

    dialog.exec();

    if (!picked.isValid())
        return QColor();   // 取消/直接关闭：视为未选

    // Ela 的取色器只给 RGB，alpha 可能是 0；标注颜色必须不透明，
    // 否则画出来是"透明"（看起来像没换色）。原色带透明度时保留。
    if (picked.isValid() && picked.alpha() == 0) {
        picked.setAlpha(initial.isValid() && initial.alpha() > 0 ? initial.alpha() : 255);
    }

    return picked;
}

} // namespace

namespace ElaColorPickerDialog {

QColor getColor(const QColor &initial, QWidget *parent, const QString &title)
{
    return runDialog(initial, title, parent);
}

QColor getColor(const QColor &initial, QWidget *parent, const QString &title, bool showAlpha)
{
    // ElaColorDialog 自带 alpha 通道编辑，showAlpha 仅为保持调用方语义
    Q_UNUSED(showAlpha);
    return runDialog(initial, title, parent);
}

} // namespace ElaColorPickerDialog
