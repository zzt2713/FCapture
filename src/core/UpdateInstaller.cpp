#include "core/UpdateInstaller.h"

#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

// 与 installer/FCapture.iss 的 AppId 保持一致：Inno 的卸载注册表键 = AppId + "_is1"。
// 自动更新只换文件不走安装器，Windows 设置→应用 显示的版本（DisplayVersion）
// 必须在这里同步，否则永远停留在首次安装时的版本。
const QString kUninstallKey =
    QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
                   "8E4A2C71-5F3B-4D62-9A18-3C7B6D2E9F04_is1");

QString scriptPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("FCaptureUpdate/apply-update.bat"));
}

QString logPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("FCaptureUpdate/update.log"));
}

} // namespace

namespace UpdateInstaller {

bool installAndRestart(const QString &zipPath, const QString &version, QString *error)
{
    // 版本号会拼进提权脚本的 reg 命令，只放行版本号字符，
    // 防止被投毒的 release tag 注入批处理命令
    static const QRegularExpression kSafeVersion(
        QStringLiteral("^[0-9A-Za-z._-]+$"));
    if (!kSafeVersion.match(version).hasMatch()) {
        if (error)
            *error = QStringLiteral("版本号格式异常，已取消更新：%1").arg(version);
        return false;
    }
    const QString exePath = QDir::toNativeSeparators(
        QCoreApplication::applicationFilePath());
    const QString exeDir = QDir::toNativeSeparators(AppPaths::exeDir());
    const QString zip = QDir::toNativeSeparators(zipPath);

    if (!QFileInfo::exists(zipPath)) {
        if (error)
            *error = QStringLiteral("找不到更新包：%1").arg(zip);
        return false;
    }

    const QFileInfo scriptInfo(scriptPath());
    QDir().mkpath(scriptInfo.absolutePath());

    QFile script(scriptPath());
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法写入更新脚本：%1").arg(scriptPath());
        return false;
    }

    // 批处理用 GBK 编码写（Windows 控制台默认代码页），避免中文路径乱码。
    // 脚本内容只用到路径变量与 PowerShell 命令，不含需要本地化的文本。
    QTextStream out(&script);
    out.setEncoding(QStringConverter::System);

    const QString work = QDir::toNativeSeparators(
        QFileInfo(zipPath).absolutePath());
    const QString backupDir = work + QStringLiteral("\\backup");

    out << "@echo off\r\n"
        << "chcp 65001 >nul\r\n"
        << "setlocal\r\n"
        << "set LOG=\"" << logPath() << "\"\r\n"
        << "echo [%date% %time%] update started >> %LOG%\r\n"
        // 1) 等本进程退出（文件被占用时 rename 会失败，最多重试 30 秒）
        << "set RETRY=0\r\n"
        << ":wait\r\n"
        << "tasklist /FI \"IMAGENAME eq " << QFileInfo(exePath).fileName()
        << "\" 2>nul | find /I \"" << QFileInfo(exePath).fileName() << "\" >nul\r\n"
        << "if not errorlevel 1 (\r\n"
        << "  set /a RETRY+=1\r\n"
        << "  if %RETRY% GEQ 30 goto giveup\r\n"
        << "  timeout /t 1 /nobreak >nul\r\n"
        << "  goto wait\r\n"
        << ")\r\n"
        // 2) 备份旧目录（失败不阻塞，只是没有回滚点）
        << "if not exist \"" << backupDir << "\" mkdir \"" << backupDir << "\"\r\n"
        << "robocopy \"" << exeDir << "\" \"" << backupDir
        << "\" /E /NFL /NDL /NJH /NJS /NP >nul 2>&1\r\n"
        // 3) 解压覆盖。Expand-Archive 需要 -Force 才能覆盖已存在文件
        << "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        << "\"Expand-Archive -LiteralPath '" << zip << "' -DestinationPath '"
        << exeDir << "' -Force\" >> %LOG% 2>&1\r\n"
        << "if errorlevel 1 goto failed\r\n"
        // 4) 同步卸载注册表条目的版本号。条目存在才写：
        //    便携版没有条目，不能 reg add 凭空造一个假卸载项出来
        << "reg query \"HKLM\\" << kUninstallKey << "\" >nul 2>&1 && "
        << "reg add \"HKLM\\" << kUninstallKey << "\" /v DisplayVersion /d "
        << version << " /f >nul\r\n"
        << "reg query \"HKCU\\" << kUninstallKey << "\" >nul 2>&1 && "
        << "reg add \"HKCU\\" << kUninstallKey << "\" /v DisplayVersion /d "
        << version << " /f >nul\r\n"
        << "echo [%date% %time%] update done >> %LOG%\r\n"
        // 5) 重新启动。本脚本是提权运行的，直接 start 会把程序也提起来，
        //    交给 Shell（explorer，普通权限）拉起，保持程序权限不变
        << "powershell -NoProfile -Command "
        << "\"(New-Object -ComObject Shell.Application).ShellExecute('" << exePath
        << "')\"\r\n"
        << "goto cleanup\r\n"
        << ":failed\r\n"
        << "echo [%date% %time%] extract failed, restoring backup >> %LOG%\r\n"
        << "robocopy \"" << backupDir << "\" \"" << exeDir
        << "\" /E /NFL /NDL /NJH /NJS /NP >> %LOG% 2>&1\r\n"
        << "powershell -NoProfile -Command "
        << "\"(New-Object -ComObject Shell.Application).ShellExecute('" << exePath
        << "')\"\r\n"
        << "goto cleanup\r\n"
        << ":giveup\r\n"
        << "echo [%date% %time%] process did not exit, abort >> %LOG%\r\n"
        << ":cleanup\r\n"
        // 5) 自删：del 自己会失败，用 start 起一个新 cmd 延迟删除
        << "start /b \"\" cmd /c \"timeout /t 2 >nul & del /f /q \\\"%~f0\\\"\"\r\n"
        << "endlocal\r\n";
    script.close();

    // 提权启动脚本（一次 UAC）：覆盖 Program Files、写 HKLM 注册表都需要管理员。
    // 阻塞等 UAC 结果但不卡界面——用户拒绝就原样返回，应用不退出、可稍后重试。
    const QString psCmd =
        QStringLiteral("Start-Process -FilePath '%1' -Verb RunAs -WindowStyle Minimized")
            .arg(QDir::toNativeSeparators(scriptPath()));
    QProcess proc;
    proc.start(QStringLiteral("powershell.exe"),
               {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), psCmd});
    if (!proc.waitForStarted()) {
        if (error)
            *error = QStringLiteral("无法启动更新脚本");
        return false;
    }
    while (!proc.waitForFinished(100))
        QCoreApplication::processEvents();
    if (proc.exitCode() != 0) {
        if (error)
            *error = QStringLiteral("更新需要管理员权限（UAC 未确认），已取消更新");
        return false;
    }
    return true;
}

} // namespace UpdateInstaller
