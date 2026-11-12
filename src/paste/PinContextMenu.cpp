#include "paste/PinContextMenu.h"

#include "core/ClipboardHandler.h"
#include "core/ImageOutput.h"
#include "paste/PinManager.h"
#include "ui/AppMenu.h"
#include "ui/OperationToast.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStandardPaths>

namespace {

QString defaultSaveName()
{
    return QStringLiteral("Snipaste_%1.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
}

} // namespace

namespace PinContextMenu {

void show(PinWindow *w, const QPoint &globalPos, QWidget *parent)
{
    if (!w)
        return;

    PinManager *mgr = PinManager::instance();

    // 用工厂创建 Ela 菜单（业务层不引入 Ela 头文件）
    AppMenu::Menu menu(parent);

    // --- 复制类 ---
    QAction *copyImg = AppMenu::addIconAction(menu.get(), ElaIconType::Copy,
                                              QStringLiteral("复制图像\tCtrl+C"));
    QAction *copyText = nullptr;
    if (!w->sourceText().isEmpty())
        copyText = AppMenu::addIconAction(menu.get(), ElaIconType::Copy,
                                          QStringLiteral("复制原文\tCtrl+Shift+C"));

    QAction *saveAs = AppMenu::addIconAction(menu.get(), ElaIconType::FloppyDisk,
                                             QStringLiteral("另存为…\tCtrl+S"));

    menu->addSeparator();

    // --- 变换 ---
    QAction *rotCW = AppMenu::addIconAction(menu.get(), ElaIconType::RotateRight,
                                            QStringLiteral("顺时针旋转 90°\t1"));
    QAction *rotCCW = AppMenu::addIconAction(menu.get(), ElaIconType::RotateRight,
                                             QStringLiteral("逆时针旋转 90°\t2"));
    QAction *flipH = AppMenu::addIconAction(menu.get(), ElaIconType::ArrowsLeftRight,
                                            QStringLiteral("水平翻转\t3"));
    QAction *flipV = AppMenu::addIconAction(menu.get(), ElaIconType::ArrowsLeftRight,
                                            QStringLiteral("垂直翻转\t4"));

    menu->addSeparator();

    QAction *gray = AppMenu::addIconAction(menu.get(), ElaIconType::Droplet,
                                          QStringLiteral("灰度\t5"));
    gray->setCheckable(true);
    gray->setChecked(false);

    QAction *invert = AppMenu::addIconAction(menu.get(), ElaIconType::CircleHalf,
                                            QStringLiteral("反色\t6"));
    invert->setCheckable(true);
    invert->setChecked(false);

    // 背景模式子菜单
    QMenu *bgMenu = AppMenu::addSubMenu(menu.get(), QStringLiteral("背景模式"));
    QAction *bgTrans = bgMenu->addAction(QStringLiteral("透明"));
    QAction *bgPseudo = bgMenu->addAction(QStringLiteral("伪透明"));
    QAction *bgDark = bgMenu->addAction(QStringLiteral("深色棋盘格"));
    QAction *bgLight = bgMenu->addAction(QStringLiteral("浅色棋盘格"));

    menu->addSeparator();

    // --- 缩放/透明度 ---
    QAction *reset = AppMenu::addIconAction(menu.get(), ElaIconType::ArrowsRotate,
                                           QStringLiteral("重置为 100%\t双击"));
    QAction *crop = AppMenu::addIconAction(menu.get(), ElaIconType::CropSimple,
                                           QStringLiteral("裁剪…"));
    QAction *thumb = AppMenu::addIconAction(menu.get(), ElaIconType::Compress,
                                            QStringLiteral("缩略图模式\tShift+双击"));

    menu->addSeparator();

    // --- 窗口属性 ---
    // 「始终置顶」默认开启（PinWindow 构造时 m_topmost = true）。
    // 勾选状态 = 当前状态，再点一次取消勾选即恢复普通窗口层级。
    QAction *topmost = AppMenu::addIconAction(menu.get(), ElaIconType::Thumbtack,
                                           QStringLiteral("始终置顶"));
    topmost->setCheckable(true);
    topmost->setChecked(w->isTopmost());

    QAction *clickThrough = AppMenu::addIconAction(menu.get(), ElaIconType::ArrowPointer,
                                                QStringLiteral("鼠标穿透"));
    clickThrough->setCheckable(true);
    clickThrough->setChecked(w->isClickThrough());

    QAction *solo = AppMenu::addIconAction(menu.get(), ElaIconType::Eye,
                                        QStringLiteral("只显示此贴图"));

    menu->addSeparator();

    QAction *closeAct = AppMenu::addIconAction(menu.get(), ElaIconType::Xmark,
                                               QStringLiteral("关闭\tCtrl+W"));
    QAction *destroyAct = AppMenu::addIconAction(menu.get(), ElaIconType::Trash,
                                                 QStringLiteral("销毁\tShift+Esc"));

    // ---- 执行 ----
    QAction *chosen = menu->exec(globalPos);
    if (!chosen)
        return;

    if (chosen == copyImg) {
        QApplication::clipboard()->setImage(w->displayImage());
        OperationToast::imageCopied();
    } else if (chosen == copyText && copyText) {
        QApplication::clipboard()->setText(w->sourceText());
    } else if (chosen == saveAs) {
        const QString path = QFileDialog::getSaveFileName(
            parent, QStringLiteral("保存图片"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                + QLatin1Char('/') + defaultSaveName(),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg);;WebP 图片 (*.webp)"));
        if (!path.isEmpty()) {
            if (ImageOutput::save(w->displayImage(), path))
                OperationToast::imageSaved();
            else
                qWarning() << "PinContextMenu: failed to save image to" << path;
        }

    } else if (chosen == rotCW) {
        w->rotateCW();
    } else if (chosen == rotCCW) {
        w->rotateCCW();
    } else if (chosen == flipH) {
        w->flipHorizontal();
    } else if (chosen == flipV) {
        w->flipVertical();
    } else if (chosen == gray) {
        w->toggleGrayscale();
    } else if (chosen == invert) {
        w->toggleInverted();

    } else if (chosen == bgTrans) {
        w->setBackgroundMode(PinWindow::BackgroundMode::Transparent);
    } else if (chosen == bgPseudo) {
        w->setBackgroundMode(PinWindow::BackgroundMode::PseudoTransparent);
    } else if (chosen == bgDark) {
        w->setBackgroundMode(PinWindow::BackgroundMode::DarkChessboard);
    } else if (chosen == bgLight) {
        w->setBackgroundMode(PinWindow::BackgroundMode::LightChessboard);

    } else if (chosen == reset) {
        w->resetTransform();
    } else if (chosen == crop) {
        w->enterCropMode();
    } else if (chosen == thumb) {
        // 缩略图模式：缩小到固定尺寸
        w->setScale(0.35);

    } else if (chosen == topmost) {
        w->setTopmost(!w->isTopmost());
    } else if (chosen == clickThrough) {
        w->setClickThrough(!w->isClickThrough());
    } else if (chosen == solo) {
        mgr->soloWindow(w);

    } else if (chosen == closeAct) {
        mgr->closeWindow(w);
    } else if (chosen == destroyAct) {
        mgr->destroyWindow(w);
    }
}

} // namespace PinContextMenu
