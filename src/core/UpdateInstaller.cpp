#include "core/UpdateInstaller.h"

#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

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

bool canInstall(QString *error)
{
    const QDir dir(AppPaths::exeDir());

    // 1) 安装目录必须可写。临时写一个探针文件验证，比检查属性可靠
    //    （Program Files 下即使属性看起来可写，实际也会被 UAC 拦）。
    const QString probe = dir.filePath(QStringLiteral(".update-probe"));
    QFile file(probe);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("安装目录不可写，请把程序放到有写权限的位置，"
                                    "或以管理员身份运行：%1")
                         .arg(dir.absolutePath());
        return false;
    }
    file.close();
    QFile::remove(probe);

    return true;
}

bool installAndRestart(const QString &zipPath, QString *error)
{
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
        // 4) 重新启动
        << "start \"\" \"" << exePath << "\"\r\n"
        << "echo [%date% %time%] update done >> %LOG%\r\n"
        << "goto cleanup\r\n"
        << ":failed\r\n"
        << "echo [%date% %time%] extract failed, restoring backup >> %LOG%\r\n"
        << "robocopy \"" << backupDir << "\" \"" << exeDir
        << "\" /E /NFL /NDL /NJH /NJS /NP >> %LOG% 2>&1\r\n"
        << "start \"\" \"" << exePath << "\"\r\n"
        << "goto cleanup\r\n"
        << ":giveup\r\n"
        << "echo [%date% %time%] process did not exit, abort >> %LOG%\r\n"
        << ":cleanup\r\n"
        // 5) 自删：del 自己会失败，用 start 起一个新 cmd 延迟删除
        << "start /b \"\" cmd /c \"timeout /t 2 >nul & del /f /q \\\"%~f0\\\"\"\r\n"
        << "endlocal\r\n";
    script.close();

    // 以隐藏窗口启动脚本，立即返回
    const bool started = QProcess::startDetached(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"), QDir::toNativeSeparators(scriptPath())});
    if (!started) {
        if (error)
            *error = QStringLiteral("无法启动更新脚本");
        return false;
    }
    return true;
}

} // namespace UpdateInstaller
