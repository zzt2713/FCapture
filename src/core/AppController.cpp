#include "core/AppController.h"

#include "core/ClipboardHandler.h"
#include "core/CommandLine.h"
#include "core/ConfigManager.h"
#include "core/HotkeyManager.h"
#include "core/ImageOutput.h"
#include "core/SingleInstance.h"
#include "core/TrayController.h"
#include "paste/PinManager.h"
#include "platform/win/HotkeyHook.h"
#include "platform/win/SoundPlayer.h"
#include "tools/HotCorner.h"
#include "tools/Whiteboard.h"
#include "ui/GroupManagerDialog.h"
#include "ui/OperationToast.h"
#include "ui/PreferenceDialog.h"
#include "paste/PinWindow.h"
#include "platform/win/WinUtil.h"
#include "snip/ScreenCapturer.h"
#include "snip/SnipHistory.h"
#include "snip/SnipOverlay.h"
#include "tools/OcrEngine.h"
#include "tools/TextToImage.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QScreen>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

const QStringList &hotkeyActions()
{
    static const QStringList actions = {
        QStringLiteral("capture"),
        QStringLiteral("capture-active-window"),
        QStringLiteral("paste"),
        QStringLiteral("toggle-images"),
    };
    return actions;
}

} // namespace

AppController *AppController::instance()
{
    static AppController s_instance;
    return &s_instance;
}

AppController::AppController(QObject *parent)
    : QObject(parent)
{
}

void AppController::initialize()
{
    m_singleInstance = new SingleInstance(this);
    connect(m_singleInstance, &SingleInstance::receiveMessage,
            this, &AppController::handleCommandLine);
    m_singleInstance->listen();

    m_tray = new TrayController(this);
    if (ConfigManager::instance()->value(QStringLiteral("general/hide_tray_icon")).toBool())
        m_tray->hideTrayIcon();
    connect(m_tray, &TrayController::captureRequested, this, &AppController::startCapture);
    connect(m_tray, &TrayController::pasteRequested, this, &AppController::startPaste);
    connect(m_tray, &TrayController::toggleImagesRequested, this, &AppController::toggleAllImages);
    connect(m_tray, &TrayController::preferencesRequested, this, &AppController::showPreferences);
    connect(m_tray, &TrayController::groupManagerRequested, this, &AppController::showGroupManager);
    connect(m_tray, &TrayController::whiteboardRequested, this,
            [this]() { showWhiteboard(QString(), false); });
    connect(m_tray, &TrayController::restartRequested, this, &AppController::restartApp);
    connect(m_tray, &TrayController::quitRequested, this, &AppController::quitApp);

    m_hotkeys = new HotkeyManager(this);
    connect(m_hotkeys, &HotkeyManager::hotkeyPressed, this, [this](const QString &action) {
        if (action == QStringLiteral("capture"))
            startCapture();
        else if (action == QStringLiteral("capture-active-window"))
            startActiveWindowCapture();
        else if (action == QStringLiteral("paste"))
            startPaste();
        else if (action == QStringLiteral("toggle-images"))
            toggleAllImages();
    });

    registerHotkeys();

    // ---- P5：热角 + Super-snip ----
    m_hotCorners = new HotCorner(this);
    connect(m_hotCorners, &HotCorner::actionTriggered, this, [this](HotCorner::Action action) {
        switch (action) {
        case HotCorner::Action::HideAllPins: PinManager::instance()->hideAll(); break;
        case HotCorner::Action::ShowAllPins: PinManager::instance()->showAll(); break;
        case HotCorner::Action::TogglePins:  toggleAllImages(); break;
        case HotCorner::Action::Capture:     startCapture(); break;
        case HotCorner::Action::Paste:       startPaste(); break;
        case HotCorner::Action::None:        break;
        }
    });

    m_mouseHook = new MouseHook(this);
    connect(m_mouseHook, &MouseHook::superSnipFinished, this,
            [this](const QPoint &from, const QPoint &to) {
                const QRect rect = QRect(from, to).normalized();
                if (rect.width() < 8 || rect.height() < 8)
                    return;
                CommandRequest req;
                req.command = CommandRequest::Command::Snip;
                req.area = rect;
                req.hasArea = true;
                startCaptureWith(req);
            });
    if (ConfigManager::instance()->value(QStringLiteral("super_snip/enabled")).toBool()) {
        if (!m_mouseHook->install())
            qWarning() << "AppController: super-snip mouse hook failed to install";
    }

    qInfo() << "AppController: initialized";
}

void AppController::registerHotkeys()
{
    ConfigManager *cfg = ConfigManager::instance();

    QStringList failed;
    m_hotkeyConflicts.clear();
    for (const QString &action : hotkeyActions()) {
        const QString key = cfg->hotkey(action);
        if (m_hotkeys->registerHotkey(action, key)
            != HotkeyManager::RegistrationStatus::Success) {
            failed << action;
            m_hotkeyConflicts.insert(action);
        }
    }

    if (!failed.isEmpty()) {
        qWarning() << "AppController: hotkey registration failed:" << failed
                   << "config:" << QStringLiteral("%APPDATA%/FCapture/config.ini");
    }

    syncHotkeyState();
}

void AppController::restoreHotkeys()
{
    if (!m_hotkeys || !m_hotkeys->isSuspended())
        return;

    const QStringList failed = m_hotkeys->resume();
    for (const QString &action : hotkeyActions()) {
        if (failed.contains(action))
            m_hotkeyConflicts.insert(action);
        else
            m_hotkeyConflicts.remove(action);
    }

    if (!failed.isEmpty())
        qWarning() << "AppController: hotkey restore failed:" << failed;
    syncHotkeyState();
}

void AppController::syncHotkeyState()
{
    if (m_tray)
        m_tray->setHotkeyConflicts(m_hotkeyConflicts);
    if (m_preferenceDialog)
        m_preferenceDialog->setHotkeyConflicts(m_hotkeyConflicts);
}

void AppController::startCapture()
{
    if (m_capturing)
        return;

    m_capturing = true;
    m_captureActiveWindow = false;
    captureCommon();
}

// Ctrl+Shift+A：直接抓活动窗口。
// 必须在这里（截图层弹出之前）取前台窗口矩形，否则截图层一 show 就变成前台了。
void AppController::startActiveWindowCapture()
{
    if (m_capturing)
        return;

    m_capturing = true;
    m_captureActiveWindow = true;
    m_activeWindowRect = WinUtil::activeWindowRect();
    captureCommon();
}

void AppController::captureCommon()
{
    // 先隐藏当前截图层（如果还在），再抓屏
    if (m_overlay)
        m_overlay->hide();

    // 让待处理的绘制/显示先完成，否则会把残留画面拍进去
    QApplication::processEvents(QEventLoop::AllEvents, 50);
    QTimer::singleShot(70, this, [this]() {
        // 始终抓不含鼠标的底图，由 SnipOverlay 按 ` / ~ 实时决定是否合成鼠标。
        const ScreenCapturer::Snapshot snap = ScreenCapturer::captureAllScreens(false);
        if (!snap.isValid()) {
            qWarning() << "AppController: screen capture returned empty snapshot";
            m_capturing = false;
            return;
        }

        auto *overlay = new SnipOverlay();
        m_overlay = overlay;
        connect(overlay, &SnipOverlay::snipFinished, this, &AppController::onSnipFinished);
        connect(overlay, &SnipOverlay::snipAborted, this, [this]() { m_capturing = false; });
        connect(overlay, &QObject::destroyed, this, [this]() { m_capturing = false; });

        overlay->startSession(snap);
        if (m_captureActiveWindow)
            overlay->captureActiveWindow(m_activeWindowRect);
        if (m_hasPendingRequest)
            applySnipRequest(overlay);
        // 截图窗口不抢任务栏
        WinUtil::applyToolWindowStyle(overlay->winId());
    });
}

void AppController::onSnipFinished(const SnipResult &result, SnipOutput output)
{
    m_capturing = false;

    if (!result.valid)
        return;

    if (ConfigManager::instance()->value(QStringLiteral("general/play_sound")).toBool()) {
        QString soundError;
        if (!SoundPlayer::playCompletionSound(&soundError)) {
            qWarning() << "AppController: completion sound failed:" << soundError;
            WinUtil::beep();
        }
    }

    // 命令行请求：按 -o 列表执行，不再走默认输出
    if (m_hasPendingRequest) {
        executeSnipOutputs(result);
        emit snipCompleted(result);
        return;
    }

    switch (output) {
    case SnipOutput::Copy:
        break;
    case SnipOutput::FileDialog:
    case SnipOutput::Save:
        break;
    case SnipOutput::QuickSave:
        break;
    case SnipOutput::Pin: {
        // 截图是物理像素，必须按屏幕 dpr 换算成逻辑尺寸，否则会被放大
        const QPoint origin = result.logicalRect.topLeft();
        qreal dpr = 1.0;
        if (QScreen *screen = QGuiApplication::screenAt(origin))
            dpr = screen->devicePixelRatio();

        // 贴到截图时的原始位置（useExactPos=true），而不是光标处
        PinWindow *w = PinManager::instance()->pasteImage(
            result.image, origin, QString(), dpr, /*useExactPos=*/true);
        if (!w)
            qWarning() << "AppController: failed to pin completed snip";
        break;
    }
    }

    // 自动保存：截图完成后静默保存一份
    if (ConfigManager::instance()->autoSaveEnabled()) {
        const QString folder = ConfigManager::instance()->autoSaveFolder();
        if (!folder.isEmpty()) {
            QImage saveImage = result.image;
            if (ConfigManager::instance()->value(QStringLiteral("output/auto_alpha_channel_removal")).toBool()
                && saveImage.hasAlphaChannel()) {
                bool hasAlpha = false;
                for (int y = 0; y < saveImage.height() && !hasAlpha; ++y) {
                    for (int x = 0; x < saveImage.width(); ++x) {
                        if (qAlpha(saveImage.pixel(x, y)) < 255) { hasAlpha = true; break; }
                    }
                }
                if (!hasAlpha)
                    saveImage = saveImage.convertToFormat(QImage::Format_RGB32);
            }
            QDir().mkpath(folder);
            const QString fmt = ConfigManager::instance()
                                    ->value(QStringLiteral("output/file_name_format"),
                                            QStringLiteral("Snipaste_{yyyy}-{MM}-{dd}_{HH}-{mm}-{ss}"))
                                    .toString();
            QString filename = fmt;
            const QDateTime now = QDateTime::currentDateTime();
            filename.replace(QStringLiteral("{yyyy}"), now.toString(QStringLiteral("yyyy")));
            filename.replace(QStringLiteral("{MM}"), now.toString(QStringLiteral("MM")));
            filename.replace(QStringLiteral("{dd}"), now.toString(QStringLiteral("dd")));
            filename.replace(QStringLiteral("{HH}"), now.toString(QStringLiteral("HH")));
            filename.replace(QStringLiteral("{mm}"), now.toString(QStringLiteral("mm")));
            filename.replace(QStringLiteral("{ss}"), now.toString(QStringLiteral("ss")));
            filename.replace(QStringLiteral("{counter}"), QString::number(
                ConfigManager::instance()->value(QStringLiteral("output/counter"), 0).toInt() + 1));

            QString ext = ConfigManager::instance()
                              ->value(QStringLiteral("output/format"), QStringLiteral("png"))
                              .toString().toLower();
            if (ext != QStringLiteral("png") && ext != QStringLiteral("jpg") && ext != QStringLiteral("webp"))
                ext = QStringLiteral("png");

            const QString path = QDir(folder).filePath(filename + QLatin1Char('.') + ext);
            if (!ImageOutput::save(saveImage, path)) {
                qWarning() << "AppController: failed to auto-save image to" << path;
            } else {
                OperationToast::imageSaved();
            }
        }
    }

    emit snipCompleted(result);
}

void AppController::startPaste()
{
    // 1) 先试试网页图片 URL
    if (ConfigManager::instance()->value(QStringLiteral("general/enable_web_image_paste")).toBool()) {
        const QStringList urls = ClipboardHandler::webImageUrls(QApplication::clipboard()->mimeData());
        if (!urls.isEmpty()) {
            PinManager::instance()->pasteFromWebImageUrls(urls);
            return;
        }
    }

    // 2) 正常剪贴板内容
    PinWindow *w = PinManager::instance()->pasteFromClipboard();
    if (!w) {
        qInfo() << "AppController: clipboard has no pinnable content";
        return;
    }
}

void AppController::toggleAllImages()
{
    PinManager *mgr = PinManager::instance();
    if (mgr->windowCount() == 0) {
        qInfo() << "AppController: no pin windows to toggle";
        return;
    }
    mgr->toggleAll();
}

void AppController::showPreferences()
{
    if (!m_preferenceDialog) {
        auto *dialog = new PreferenceDialog(nullptr);
        m_preferenceDialog = dialog;
        // 关闭即销毁：首选项有 10 个页面、大量 Ela 控件，
        // 常驻会占十几 MB 且永不回落。重新打开时重建，代价远小于常驻内存。
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        dialog->setHotkeyConflicts(m_hotkeyConflicts);

        connect(dialog, &PreferenceDialog::hotkeyRecordingChanged, this,
                [this](bool recording) {
                    if (!m_hotkeys)
                        return;
                    if (recording)
                        m_hotkeys->suspend();
                    else
                        restoreHotkeys();
                });
        connect(dialog, &PreferenceDialog::restartRequested, this, &AppController::restartApp);
        connect(dialog, &PreferenceDialog::hotkeyChangeRequested, this,
                [this, dialog](const QString &action, const QString &hotkey) {
                    if (!m_hotkeys) {
                        dialog->showHotkeyResult(action, false,
                                                 QStringLiteral("快捷键被占用"));
                        return;
                    }

                    if (m_hotkeys->registerHotkey(action, hotkey)
                        != HotkeyManager::RegistrationStatus::Success) {
                        restoreHotkeys();
                        m_hotkeyConflicts.insert(action);
                        syncHotkeyState();
                        dialog->showHotkeyResult(action, false,
                                                 QStringLiteral("快捷键被占用"));
                        return;
                    }

                    ConfigManager::instance()->setHotkey(action, hotkey);
                    m_hotkeyConflicts.remove(action);
                    syncHotkeyState();
                    dialog->showHotkeyResult(action, true);
                });
        connect(dialog, &QObject::destroyed, this, [this]() {
            restoreHotkeys();
        });
    }

    m_preferenceDialog->show();
    m_preferenceDialog->raise();
    m_preferenceDialog->activateWindow();
}

void AppController::showGroupManager()
{
    auto *dialog = new GroupManagerDialog(nullptr);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void AppController::showWhiteboard(const QString &color, bool transparent)
{
    if (m_whiteboard) {
        m_whiteboard->close();
        m_whiteboard = nullptr;
    }

    QColor background = Qt::white;
    if (!color.isEmpty()) {
        const QColor parsed(color);
        if (parsed.isValid())
            background = parsed;
    }

    auto *board = new Whiteboard(background, transparent);
    m_whiteboard = board;
    connect(board, &QObject::destroyed, this, [this]() { m_whiteboard = nullptr; });
    board->showOnActiveScreen();
}

void AppController::startPickColor()
{
    // 复用截图流程的取色：进入截图层后按 C 取色
    startCapture();
}

// 把剪贴板的纯文本/HTML渲染成图片再贴出
void AppController::pasteAsImage()
{
    QClipboard *clip = QApplication::clipboard();
    if (!clip)
        return;
    const QString text = clip->text();
    if (text.isEmpty())
        return;

    TextToImage::Options opt;
    const QImage img = TextToImage::renderPlain(text, opt);
    if (img.isNull())
        return;
    PinManager::instance()->pasteImage(img, QCursor::pos(), QStringLiteral("paste-as-image"));
}

void AppController::pasteAsPlainText()
{
    // 把剪贴板里的富文本转成纯文本再放回去，然后让用户自己 Ctrl+V
    QClipboard *clip = QApplication::clipboard();
    if (!clip)
        return;
    const QString plain = clip->text();
    if (!plain.isEmpty())
        clip->setText(plain);
}

void AppController::quitApp()
{
    if (m_overlay)
        m_overlay->close();

    // 必须在 QApplication 销毁之前清掉所有贴图窗口。
    // PinManager 是静态单例，析构时机晚于 QApplication；
    // 那时再 deleteLater 窗口会访问已销毁的 GUI 资源，导致退出时段错误。
    PinManager::instance()->destroyAll();
    SoundPlayer::shutdown();

    QApplication::quit();
}

void AppController::restartApp()
{
    const QString executable = QDir::toNativeSeparators(
        QCoreApplication::applicationFilePath());
    const QString escaped = QString(executable).replace(QLatin1Char('\''),
                                                        QStringLiteral("''"));
    const QString script = QStringLiteral(
        "Start-Sleep -Milliseconds 350; Start-Process -FilePath '%1'").arg(escaped);
    const bool started = QProcess::startDetached(
        QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-WindowStyle"),
         QStringLiteral("Hidden"), QStringLiteral("-Command"), script});
    if (!started) {
        qWarning() << "AppController: failed to schedule application restart";
        return;
    }

    quitApp();
}

void AppController::handleCommandLine(const QStringList &args)
{
    if (args.size() <= 1)
        return;
    runCommand(args);
}

// ---------------------------------------------------------------- 命令行执行

void AppController::runCommand(const QStringList &args)
{
    const CommandRequest req = CommandLine::parse(args);
    if (!req.ok()) {
        qWarning() << "AppController: command line error:" << req.error;
        return;
    }

    using Cmd = CommandRequest::Command;
    switch (req.command) {
    case Cmd::Snip:
        startCaptureWith(req);
        break;
    case Cmd::Paste: {
        PinManager *mgr = PinManager::instance();
        const QPoint pos = req.hasPastePos ? req.pastePos : QPoint();
        bool handled = false;
        if (!req.pasteText.isEmpty()) {
            const auto payload = ClipboardHandler::fromText(req.pasteText);
            if (payload.valid) {
                mgr->pasteImage(payload.image, pos, payload.sourceText);
                handled = true;
            }
        }
        if (!req.pasteHtml.isEmpty()) {
            const auto payload = ClipboardHandler::fromHtml(req.pasteHtml);
            if (payload.valid) {
                mgr->pasteImage(payload.image, pos, payload.sourceText);
                handled = true;
            }
        }
        for (const QString &file : req.pasteFiles) {
            QImage img(file);
            if (!img.isNull()) {
                mgr->pasteImage(img, pos);
                handled = true;
            }
        }
        if (!handled)
            startPaste();
        break;
    }
    case Cmd::ToggleImages:
        toggleAllImages();
        break;
    case Cmd::ShowImages:
        PinManager::instance()->showAll();
        break;
    case Cmd::HideImages:
        PinManager::instance()->hideAll();
        break;
    case Cmd::CreateGroup:
        PinManager::instance()->createGroup(req.groupName.isEmpty()
                                                ? QStringLiteral("Group")
                                                : req.groupName);
        break;
    case Cmd::SwitchGroup:
        PinManager::instance()->switchGroup(req.groupName);
        break;
    case Cmd::SwitchGroupPrev:
        PinManager::instance()->switchToNextGroup();
        break;
    case Cmd::EmptyGroup:
        PinManager::instance()->emptyActiveGroup();
        break;
    case Cmd::ShowGroupManager:
        showGroupManager();
        break;
    case Cmd::Whiteboard:
        showWhiteboard(req.color, req.transparentBackground);
        break;
    case Cmd::PickColor:
        startPickColor();
        break;
    case Cmd::BarcodeScan:
    case Cmd::BarcodeDecode:
        qWarning() << "AppController: barcode support requires zxing-cpp";
        break;
    case Cmd::OpenPreferences:
        showPreferences();
        break;
    case Cmd::Docs:
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(QStringLiteral("docs/功能清单.md")).absoluteFilePath()));
        break;
    case Cmd::Exit:
        quitApp();
        break;
    case Cmd::None:
        break;
    }
}

void AppController::startCaptureWith(const CommandRequest &req)
{
    if (m_capturing)
        return;

    m_pendingRequest = req;
    m_hasPendingRequest = true;

    // --active-window 走专门路径（要在截图层弹出前取前台窗口）
    if (req.activeWindow) {
        startActiveWindowCapture();
        return;
    }

    m_capturing = true;
    m_captureActiveWindow = false;

    if (req.delayMs > 0) {
        QTimer::singleShot(req.delayMs, this, [this]() { captureCommon(); });
    } else {
        captureCommon();
    }
}

// 把命令行参数落到已经建好的截图层上
void AppController::applySnipRequest(SnipOverlay *overlay)
{
    if (!overlay)
        return;

    const CommandRequest &req = m_pendingRequest;

    if (req.hold)
        overlay->setHoldMode(true);

    if (req.hasArea) {
        overlay->presetArea(req.area);
    } else if (req.fullScreen) {
        if (QScreen *screen = QGuiApplication::primaryScreen())
            overlay->presetArea(screen->geometry());
    } else if (req.lastRegion) {
        const QRect last = SnipHistory::instance()->lastRect();
        if (!last.isEmpty())
            overlay->presetArea(last);
    } else if (req.activeScreen) {
        if (const QScreen *screen = QGuiApplication::screenAt(QCursor::pos()))
            overlay->presetArea(screen->geometry());
    }
}

// -o 多输出的执行
void AppController::executeSnipOutputs(const SnipResult &result)
{
    const QStringList outputs = CommandLine::normalizeOutputs(m_pendingRequest.outputs);

    // 自动移除 alpha 通道（若图不含透明像素）
    QImage saveImage = result.image;
    if (ConfigManager::instance()->value(QStringLiteral("output/auto_alpha_channel_removal")).toBool()
        && saveImage.hasAlphaChannel()) {
        bool hasAlpha = false;
        for (int y = 0; y < saveImage.height() && !hasAlpha; ++y) {
            for (int x = 0; x < saveImage.width(); ++x) {
                if (qAlpha(saveImage.pixel(x, y)) < 255) {
                    hasAlpha = true;
                    break;
                }
            }
        }
        if (!hasAlpha)
            saveImage = saveImage.convertToFormat(QImage::Format_RGB32);
    }

    for (const QString &o : outputs) {
        const QString lower = o.toLower();
        if (lower == QStringLiteral("clipboard")) {
            QApplication::clipboard()->setImage(saveImage);
            OperationToast::imageCopied();
        } else if (lower == QStringLiteral("ocr_clipboard")) {
            auto *ocr = new OcrEngine(this);
            connect(ocr, &OcrEngine::finished, this, [ocr](const WinOcr::Result &r) {
                if (r.ok)
                    QApplication::clipboard()->setText(r.text);
                ocr->deleteLater();
            });
            connect(ocr, &OcrEngine::failed, this, [ocr](const QString &) {
                ocr->deleteLater();
            });
            ocr->recognize(result.image);
        } else if (lower == QStringLiteral("pin")) {
            qreal dpr = 1.0;
            if (QScreen *screen = QGuiApplication::screenAt(result.logicalRect.topLeft()))
                dpr = screen->devicePixelRatio();
            PinManager::instance()->pasteImage(result.image, result.logicalRect.topLeft(),
                                               QString(), dpr, true);
        } else if (lower == QStringLiteral("quick-save")) {
            const QString folder = ConfigManager::instance()->quickSaveFolder();
            QDir().mkpath(folder);
            const QString path = QDir(folder).filePath(
                QStringLiteral("Snipaste_%1.png")
                    .arg(QDateTime::currentDateTime().toString(
                        QStringLiteral("yyyy-MM-dd_HH-mm-ss"))));
            if (!ImageOutput::save(saveImage, path)) {
                qWarning() << "AppController: failed to quick-save image to" << path;
            } else {
                OperationToast::imageSaved();
            }
        } else if (lower == QStringLiteral("file-dialog")) {
            const QString path = QFileDialog::getSaveFileName(
                nullptr, QStringLiteral("保存截图"),
                QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                    .filePath(QStringLiteral("Snipaste.png")),
                QStringLiteral("PNG 图片 (*.png)"));
            if (!path.isEmpty()) {
                if (ImageOutput::save(saveImage, path))
                    OperationToast::imageSaved();
                else
                    qWarning() << "AppController: failed to save image to" << path;
            }
        } else if (lower == QStringLiteral("printer")) {
            qWarning() << "AppController: printer output not implemented";
        } else if (lower.startsWith(QStringLiteral("exec("))) {
            const QString cmd = o.mid(5, o.size() - 6);
            QProcess::startDetached(cmd, {});
        } else {
            const QString path = QDir::toNativeSeparators(o);
            if (!ImageOutput::save(saveImage, path)) {
                qWarning() << "AppController: failed to save to" << path;
            } else {
                OperationToast::imageSaved();
            }
        }
    }

    m_hasPendingRequest = false;
    m_pendingRequest = CommandRequest();
}
