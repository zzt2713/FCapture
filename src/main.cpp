#include "core/AppController.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/SingleInstance.h"
#include "core/Watchdog.h"
#include "paste/PinManager.h"
#include "platform/win/WinCrashHandler.h"
#include "platform/win/WinUtil.h"
#include "ui/ElaThemeBridge.h"
#include "ui/IconFactory.h"
#include "ui/Theme.h"

// ElaWidgetTools 只允许在 main.cpp 和 src/ui/ 内引入
#include "ElaApplication.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyleFactory>
#include <QThreadPool>
#include <QTimer>

namespace {

// 从命令行取 --log-level=debug（也接受 --log-level debug 这种空格式）
bool debugLoggingRequested(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.compare(QStringLiteral("--log-level=debug"), Qt::CaseInsensitive) == 0)
            return true;
        if (arg.compare(QStringLiteral("--debug-log"), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// 上次异常退出：提示用户并提供 minidump 位置
void warnAboutPreviousCrash()
{
    const QString marker = WinCrashHandler::consumeCrashMarker();
    if (marker.isEmpty())
        return;
    qWarning() << "Previous run crashed:" << marker;
    // 启动早期没有窗口可挂 ElaMessageBar，用标准对话框并临时套截图层暗色样式，
    // 避免出现刺眼的白底消息框。
    QMessageBox box(QMessageBox::Warning, QStringLiteral("上次运行异常退出"),
                    QStringLiteral("检测到上次运行没有正常结束。\n\n"
                                   "崩溃转储和日志已保存在数据目录，"
                                   "可在首选项的「关于」页一次性导出诊断信息。\n\n%1")
                        .arg(marker.left(600)),
                    QMessageBox::Ok);
    box.setStyleSheet(Theme::overlayStyleSheet());
    box.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    // 必须在 QApplication 之前声明 DPI 感知
    WinUtil::setProcessDpiAware();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("FCapture"));
    QApplication::setOrganizationDomain(QStringLiteral("fcapture.local"));
    QApplication::setApplicationName(QStringLiteral("FCapture"));
    // 版本号由 CMake 的 project(VERSION) 注入，避免两处手写不一致
#ifdef FCAPTURE_VERSION
    QApplication::setApplicationVersion(QStringLiteral(FCAPTURE_VERSION));
#else
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));
#endif
    QApplication::setQuitOnLastWindowClosed(false);

    // 改名后首次启动：把旧的 SnipasteClone 数据目录整份搬过来
    AppPaths::migrateLegacyDataIfNeeded();

    // 限制全局线程池：本项目只有 OCR 用到它，默认上限会按 CPU 核数
    // 预建一堆线程（每个 1MB 栈）。1 个就够，识别本来就是串行的。
    QThreadPool::globalInstance()->setMaxThreadCount(1);

    Logger::init(debugLoggingRequested(argc, argv));
    Logger::installCrashHandler();
    qInfo() << "=== F-Capture" << QApplication::applicationVersion() << "starting ===";

    // 顺序不能错：QApplication → Fusion → eApp->init() → 任何 Ela 控件
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(Theme::appFont());
    app.setStyleSheet(Theme::baseStyleSheet());
    app.setWindowIcon(IconFactory::appIcon());

    // Ela 初始化：必须早于任何 ElaXxx 控件构造，否则图标全部变方框
    eApp->init();
    ElaThemeBridge::instance()->initialize();

    // 单实例：已有实例在监听则转发命令行并退出
    SingleInstance forwarder;
    if (forwarder.tryConnect(app.arguments())) {
        qInfo() << "Another instance is running, forwarded command line and exiting";
        return 0;
    }

    // 主实例：AppController 内部创建 SingleInstance 并 listen()
    AppController::instance()->initialize();

    // 主线程看门狗：每 2 秒打点，超过 5 秒未打点写 WARN（不重启，避免掩盖问题）
    Watchdog watchdog;
    watchdog.start();

    // 崩溃提示放到事件循环开始后，避免阻塞启动
    QTimer::singleShot(0, &app, warnAboutPreviousCrash);

    const int ret = app.exec();

    watchdog.stop();

    // 兜底：确保贴图窗口在 QApplication 还活着时清理干净
    PinManager::instance()->destroyAll();

    qInfo() << "=== F-Capture exited with" << ret << "===";
    return ret;
}
