#include "ui/OperationToast.h"

#include "core/ConfigManager.h"

#include "ElaToast.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>

#include <functional>

namespace {

constexpr int kToastDisplayMsec = 2000;
constexpr int kToastScreenMargin = 24;

QSet<QWidget *> snapshotTopLevels()
{
    QSet<QWidget *> existingTopLevels;
    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevels)
        existingTopLevels.insert(widget);
    return existingTopLevels;
}

// 把新建的 toast 挪到给定位置
void placeNewToast(const QSet<QWidget *> &existing, const std::function<QPoint(ElaToast *)> &pos)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        auto *toast = qobject_cast<ElaToast *>(widget);
        if (!toast || existing.contains(widget))
            continue;
        toast->move(pos(toast));
        break;
    }
}

void showToast(const QString &text)
{
    if (QApplication::closingDown()
        || !ConfigManager::instance()
                ->value(QStringLiteral("general/show_operation_toast"))
                .toBool()) {
        return;
    }

    QSet<QWidget *> existingTopLevels;
    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevels)
        existingTopLevels.insert(widget);

    ElaToast::success(text, kToastDisplayMsec);

    for (QWidget *widget : QApplication::topLevelWidgets()) {
        auto *toast = qobject_cast<ElaToast *>(widget);
        if (!toast || existingTopLevels.contains(widget))
            continue;

        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            break;

        const QRect available = screen->availableGeometry();
        toast->move(available.x() + available.width() - toast->width() - kToastScreenMargin,
                    available.y() + available.height() - toast->height() - kToastScreenMargin);
        break;
    }
}

} // namespace

void OperationToast::imageCopied()
{
    showToast(QStringLiteral("图片已复制到剪贴板"));
}

void OperationToast::imageSaved()
{
    showToast(QStringLiteral("图片已保存"));
}

void OperationToast::centerInfo(const QString &text, int displayMsec)
{
    if (QApplication::closingDown())
        return;

    const QSet<QWidget *> existing = snapshotTopLevels();

    ElaToast::info(text, displayMsec);

    placeNewToast(existing, [](ElaToast *toast) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen)
            return QPoint(0, 0);
        const QRect available = screen->availableGeometry();
        // 屏幕正中
        return QPoint(available.x() + (available.width() - toast->width()) / 2,
                      available.y() + (available.height() - toast->height()) / 2);
    });
}
