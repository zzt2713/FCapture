#include "paste/PinManager.h"

#include "core/ClipboardHandler.h"
#include "core/ConfigManager.h"
#include "paste/PinContextMenu.h"
#include "platform/win/VirtualDesktop.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace {



// 让新窗口出现在光标处，但不超出屏幕范围。
// anchor 用图片内容位置选屏幕，避免窗口阴影临时越界时选错显示器。
//
// useWorkArea：true = 限制在工作区（availableGeometry，避开任务栏）；
// false = 限制在整块屏幕（geometry）。截图原位贴图必须传 false：
// 用户截的是任务栏那一块，贴出来就该留在任务栏位置，不能被"让位"
// 顶到任务栏上边去。
QPoint clampToScreen(const QPoint &pos, const QSize &size, const QPoint &anchor,
                     bool useWorkArea)
{
    QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen)
        screen = QGuiApplication::screenAt(pos);
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return pos;

    const QRect avail = useWorkArea ? screen->availableGeometry() : screen->geometry();
    int x = pos.x();
    int y = pos.y();
    if (x + size.width() - 1 > avail.right())
        x = avail.right() - size.width() + 1;
    if (y + size.height() - 1 > avail.bottom())
        y = avail.bottom() - size.height() + 1;
    x = qMax(avail.left(), x);
    y = qMax(avail.top(), y);
    return QPoint(x, y);
}

} // namespace

PinManager *PinManager::instance()
{
    static PinManager s_instance;
    return &s_instance;
}

PinManager::PinManager(QObject *parent)
    : QObject(parent)
{
    m_groups << QStringLiteral("Default");

    // 公开接口没有"虚拟桌面切换"通知，只能低频轮询当前桌面 ID
    m_desktopTimer = new QTimer(this);
    m_desktopTimer->setInterval(800);
    connect(m_desktopTimer, &QTimer::timeout, this, &PinManager::syncDesktopVisibility);
    if (VirtualDesktop::isSupported()) {
        m_lastDesktopId = VirtualDesktop::currentDesktopId();
        m_desktopTimer->start();
    }
}

PinManager::~PinManager()
{
    destroyAll();
}

void PinManager::syncDesktopVisibility()
{
    if (!VirtualDesktop::isSupported())
        return;

    const QUuid now = VirtualDesktop::currentDesktopId();
    if (now.isNull() || now == m_lastDesktopId)
        return;
    m_lastDesktopId = now;

    for (PinWindow *w : m_windows) {
        if (!w)
            continue;
        const bool onCurrent = w->isOnCurrentDesktop();
        if (onCurrent && !w->isVisible())
            w->show();
        else if (!onCurrent && w->isVisible())
            w->hide();
    }
}

// ---------------------------------------------------------------- 创建

void PinManager::attachWindow(PinWindow *w)
{
    if (!w)
        return;

    m_windows.append(w);
    w->setGroup(m_activeGroup);

    connect(w, &PinWindow::requestClose, this, &PinManager::closeWindow);
    connect(w, &PinWindow::requestDestroy, this, &PinManager::destroyWindow);

    connect(w, &PinWindow::clicked, this,
            [this](PinWindow *win, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
                if (button == Qt::LeftButton && mods.testFlag(Qt::ControlModifier))
                    toggleSelection(win);
                else if (button == Qt::LeftButton)
                    setSelection(win, true);
            });

    connect(w, &PinWindow::contextMenuRequested, this,
            [this](PinWindow *win, const QPoint &globalPos) {
                if (!m_selection.contains(win)) {
                    clearSelection();
                    setSelection(win, true);
                }
                PinContextMenu::show(win, globalPos);
            });

    connect(w, &QObject::destroyed, this, [this, w]() { detachWindow(w); });

    emit windowsChanged();
}

void PinManager::detachWindow(PinWindow *w)
{
    m_windows.removeAll(w);
    m_selection.removeAll(w);
    m_soloHidden.removeAll(w);
    emit windowsChanged();
    emit selectionChanged();
}

void PinManager::placementPosition(PinWindow *w, const QPoint &requested, bool useExactPos)
{
    if (!w)
        return;

    // useExactPos=true：调用方给了明确位置（如截图原位），不再偏移
    // useExactPos=false：用光标位置，并做轻微错开避免新窗口完全重叠
    QPoint contentPos = requested;
    if (!useExactPos) {
        contentPos = QCursor::pos();
        static int offset = 0;
        contentPos += QPoint(offset * 24, offset * 24);
        offset = (offset + 1) % 6;
    }

    const QRect content = w->contentRect();
    const QSize contentSize = content.size();
    const QPoint contentOffset = content.topLeft();

    QScreen *screen = QGuiApplication::screenAt(contentPos);
    if (!screen)
        screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();

    // 磁性吸附：靠近屏幕边缘/其他贴图时吸附。
    // 吸附基准必须和钳制基准一致：截图原位贴图用整块屏幕边界，
    // 否则贴到任务栏区域时会被"吸附"到工作区底边，看起来像自己往上跳。
    const int magnet = ConfigManager::instance()
                           ->value(QStringLiteral("paste/magnetic_attach_threshold"), 20).toInt();
    if (magnet > 0 && screen) {
        const QRect bounds =
            useExactPos ? screen->geometry() : screen->availableGeometry();
        if (!bounds.isEmpty()) {
            const int right = bounds.left() + bounds.width();
            const int bottom = bounds.top() + bounds.height();
            if (qAbs(contentPos.x() - bounds.left()) < magnet)
                contentPos.setX(bounds.left());
            if (qAbs(contentPos.y() - bounds.top()) < magnet)
                contentPos.setY(bounds.top());
            if (qAbs(contentPos.x() + contentSize.width() - right) < magnet)
                contentPos.setX(right - contentSize.width());
            if (qAbs(contentPos.y() + contentSize.height() - bottom) < magnet)
                contentPos.setY(bottom - contentSize.height());
        }
    }

    const QPoint windowPos = contentPos - contentOffset;
    w->move(clampToScreen(windowPos, w->size(), contentPos, /*useWorkArea=*/!useExactPos));
}

PinWindow *PinManager::pasteImage(const QImage &img, const QPoint &globalPos,
                                  const QString &sourceText, qreal imageDpr,
                                  bool useExactPos)
{
    if (img.isNull())
        return nullptr;

    auto *w = new PinWindow(img, sourceText, imageDpr);
    attachWindow(w);

    // 记住创建时的虚拟桌面：切换桌面后只在原桌面显示
    if (ConfigManager::instance()->value(QStringLiteral("snip/paste_current_desktop_only")).toBool())
        w->setDesktopId(VirtualDesktop::currentDesktopId());

    placementPosition(w, globalPos, useExactPos);
    w->show();
    w->raise();
    return w;
}

QVector<PinWindow *> PinManager::pasteImageSet(const QVector<QImage> &images)
{
    QVector<PinWindow *> out;
    for (const QImage &img : images) {
        if (PinWindow *w = pasteImage(img))
            out.append(w);
    }
    return out;
}

// 网页图片：异步下载后再贴出。失败只写日志，不阻塞 UI。
bool PinManager::pasteFromWebImageUrls(const QStringList &urls)
{
    if (urls.isEmpty())
        return false;

    const int maxMb = ConfigManager::instance()
                          ->value(QStringLiteral("misc/auto_download_max_image_size_mb"), 16)
                          .toInt();

    auto *nam = new QNetworkAccessManager(this);

    for (const QString &urlString : urls) {
        QNetworkRequest request{QUrl(urlString)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("FCapture/1.0"));

        QNetworkReply *reply = nam->get(request);

        // 超时 30 秒
        auto *timer = new QTimer(reply);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(30000);

        connect(reply, &QNetworkReply::finished, this, [this, reply, urlString, maxMb]() {
            reply->deleteLater();

            if (reply->error() != QNetworkReply::NoError) {
                qWarning() << "PinManager: web image download failed:" << urlString
                           << reply->errorString();
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.size() > maxMb * 1024 * 1024) {
                qWarning() << "PinManager: web image too large:" << data.size();
                return;
            }

            QImage img;
            if (!img.loadFromData(data)) {
                qWarning() << "PinManager: web image decode failed:" << urlString;
                return;
            }

            pasteImage(img, QPoint(), urlString);
        });
    }

    return true;
}

PinWindow *PinManager::pasteFromClipboard()
{
    // 1) 先恢复关闭的贴图
    if (!m_closedStack.isEmpty()) {
        const QImage img = m_closedStack.takeLast();
        return pasteImage(img);
    }

    // 2) 从剪贴板解析
    const QVector<ClipboardHandler::Payload> payloads = ClipboardHandler::fromClipboard();
    if (payloads.isEmpty())
        return nullptr;

    PinWindow *first = nullptr;
    for (const auto &p : payloads) {
        if (!p.valid || p.image.isNull())
            continue;
        PinWindow *w = pasteImage(p.image, QPoint(), p.sourceText);
        if (w && !first)
            first = w;
    }
    return first;
}

// ---------------------------------------------------------------- 查询

QVector<PinWindow *> PinManager::visibleWindows() const
{
    QVector<PinWindow *> out;
    for (PinWindow *w : m_windows) {
        if (w && w->isVisible())
            out.append(w);
    }
    return out;
}

QVector<PinWindow *> PinManager::selectedWindows() const
{
    QVector<PinWindow *> out;
    for (PinWindow *w : m_selection) {
        if (w)
            out.append(w);
    }
    return out;
}

bool PinManager::allVisible() const
{
    if (m_windows.isEmpty())
        return false;
    for (PinWindow *w : m_windows) {
        if (w && !w->isVisible())
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- 显示控制

void PinManager::showAll()
{
    for (PinWindow *w : m_windows) {
        if (w)
            w->show();
    }
    emit visibilityChanged(true);
}

void PinManager::hideAll()
{
    for (PinWindow *w : m_windows) {
        if (w)
            w->hide();
    }
    emit visibilityChanged(false);
}

void PinManager::toggleAll()
{
    if (visibleWindows().isEmpty())
        showAll();
    else
        hideAll();
}

void PinManager::soloWindow(PinWindow *w)
{
    if (!w)
        return;

    m_soloHidden.clear();
    for (PinWindow *other : m_windows) {
        if (other && other != w && other->isVisible()) {
            m_soloHidden.append(other);
            other->hide();
        }
    }
    w->show();
    w->raise();
}

void PinManager::restoreSolo()
{
    for (PinWindow *w : m_soloHidden) {
        if (w)
            w->show();
    }
    m_soloHidden.clear();
}

// ---------------------------------------------------------------- 关闭/销毁

void PinManager::closeWindow(PinWindow *w)
{
    if (!w)
        return;

    // 存入可恢复栈
    const QPixmap snap = w->snapshotForRecovery();
    if (!snap.isNull()) {
        m_closedStack.append(snap.toImage());
        while (m_closedStack.size() > ConfigManager::instance()
                   ->value(QStringLiteral("general/recover_closed_count"), 1).toInt())
            m_closedStack.removeFirst();
    }

    m_windows.removeAll(w);
    m_selection.removeAll(w);
    w->hide();
    w->deleteLater();

    emit windowsChanged();
    emit selectionChanged();
}

void PinManager::destroyWindow(PinWindow *w)
{
    if (!w)
        return;

    m_windows.removeAll(w);
    m_selection.removeAll(w);
    w->hide();
    w->deleteLater();

    emit windowsChanged();
    emit selectionChanged();
}

void PinManager::closeAll()
{
    const auto copy = m_windows;
    for (PinWindow *w : copy)
        closeWindow(w);
}

void PinManager::destroyAll()
{
    const auto copy = m_windows;
    m_windows.clear();
    m_selection.clear();
    m_soloHidden.clear();
    m_closedStack.clear();

    for (PinWindow *w : copy) {
        if (!w)
            continue;
        // 断开信号，避免销毁过程中的回调再访问管理器
        disconnect(w, nullptr, this, nullptr);
        w->hide();
        // 直接删除：此路径可能发生在事件循环结束之后，deleteLater 不会被执行
        delete w;
    }
}

// ---------------------------------------------------------------- 选择

void PinManager::selectAllVisible()
{
    for (PinWindow *w : m_windows) {
        if (w && w->isVisible())
            w->setSelected(true);
    }
    m_selection = visibleWindows();
    emit selectionChanged();
}

void PinManager::clearSelection()
{
    for (PinWindow *w : m_selection) {
        if (w)
            w->setSelected(false);
    }
    m_selection.clear();
    emit selectionChanged();
}

void PinManager::setSelection(PinWindow *w, bool selected)
{
    if (!w)
        return;

    if (selected) {
        if (!m_selection.contains(w)) {
            m_selection.append(w);
            w->setSelected(true);
        }
    } else {
        m_selection.removeAll(w);
        w->setSelected(false);
    }
    emit selectionChanged();
}

void PinManager::toggleSelection(PinWindow *w)
{
    if (!w)
        return;
    setSelection(w, !w->isSelected());
}

// ---------------------------------------------------------------- 分组

QStringList PinManager::groups() const
{
    return m_groups;
}

void PinManager::createGroup(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_groups.contains(trimmed))
        return;
    m_groups.append(trimmed);
    emit groupChanged(m_activeGroup);
}

void PinManager::removeGroup(const QString &name)
{
    if (name == QStringLiteral("Default") || !m_groups.contains(name))
        return;

    // 组内窗口一并销毁
    const auto copy = m_windows;
    for (PinWindow *w : copy) {
        if (w && w->group() == name)
            destroyWindow(w);
    }

    m_groups.removeAll(name);
    if (m_activeGroup == name)
        m_activeGroup = QStringLiteral("Default");

    emit groupChanged(m_activeGroup);
}

void PinManager::renameGroup(const QString &oldName, const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (oldName == QStringLiteral("Default") || trimmed.isEmpty())
        return;
    if (!m_groups.contains(oldName) || m_groups.contains(trimmed))
        return;

    m_groups[m_groups.indexOf(oldName)] = trimmed;
    for (PinWindow *w : m_windows) {
        if (w && w->group() == oldName)
            w->setGroup(trimmed);
    }
    if (m_activeGroup == oldName)
        m_activeGroup = trimmed;

    emit groupChanged(m_activeGroup);
}

void PinManager::switchGroup(const QString &name)
{
    if (!m_groups.contains(name) || name == m_activeGroup)
        return;

    m_previousGroup = m_activeGroup;
    m_activeGroup = name;

    // 隐藏其它组，显示本组
    for (PinWindow *w : m_windows) {
        if (!w)
            continue;
        if (w->group() == name)
            w->show();
        else
            w->hide();
    }

    emit groupChanged(m_activeGroup);
}

void PinManager::switchToNextGroup()
{
    if (m_groups.size() < 2)
        return;
    const int idx = m_groups.indexOf(m_activeGroup);
    switchGroup(m_groups.at((idx + 1) % m_groups.size()));
}

void PinManager::moveToGroup(PinWindow *w, const QString &group)
{
    if (!w || !m_groups.contains(group))
        return;

    w->setGroup(group);
    if (group != m_activeGroup)
        w->hide();
}

void PinManager::emptyActiveGroup()
{
    const auto copy = m_windows;
    for (PinWindow *w : copy) {
        if (w && w->group() == m_activeGroup)
            destroyWindow(w);
    }
}

// ---------------------------------------------------------------- 穿透

void PinManager::disableClickThrough()
{
    for (PinWindow *w : m_windows) {
        if (w && w->isClickThrough())
            w->setClickThrough(false);
    }
}

void PinManager::toggleClickThroughUnderCursor()
{
    const QPoint cursor = QCursor::pos();

    PinWindow *target = nullptr;
    for (PinWindow *w : m_windows) {
        if (!w || !w->isVisible())
            continue;
        const QRect content = w->contentRect();
        const QRect globalContent(w->mapToGlobal(content.topLeft()), content.size());
        if (globalContent.contains(cursor)) {
            target = w;
            break;
        }
    }

    if (target)
        target->setClickThrough(!target->isClickThrough());
    else
        disableClickThrough();
}

void PinManager::applyBackgroundMode(PinWindow::BackgroundMode mode)
{
    for (PinWindow *w : m_selection) {
        if (w)
            w->setBackgroundMode(mode);
    }
}
