#include "ui/OperationToast.h"

#include "core/ConfigManager.h"

#include "ElaToast.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>


namespace {

constexpr int kToastDisplayMsec = 2000;
constexpr int kToastScreenMargin = 24;

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

        // ElaToast 是普通顶层窗口，进程不在前台时 Windows 会把新窗口压到
        // 前台应用窗口后面（表现为"通知时有时无"）。强制置顶层 + 不抢焦点。
        toast->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        toast->setAttribute(Qt::WA_ShowWithoutActivating);
        toast->show();
        toast->raise();

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

void OperationToast::longShotStarted()
{
    showToast(QStringLiteral("开始滚动拼接，按 Esc 或「暂停」结束"));
}

void OperationToast::longShotStopped(int frames, int height)
{
    showToast(QStringLiteral("长截图完成：%1 帧，高 %2 px").arg(frames).arg(height));
}
