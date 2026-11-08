#include "core/Diagnostics.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/ZipStoreWriter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// 单个文件超过 32MB 就不带（日志轮转文件、minidump 可能很大）
constexpr qint64 kMaxFileSize = 32LL * 1024 * 1024;

// 把目录下文件读进 zip，内部路径为 prefix/文件名
void addDir(ZipStoreWriter &zip, const QString &dir, const QString &prefix)
{
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files) {
        if (fi.size() > kMaxFileSize)
            continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        zip.addFile(prefix + QLatin1Char('/') + fi.fileName(), f.readAll());
    }
}

QString systemSummary()
{
    QString s;
    QTextStream out(&s);
    out << "F-Capture " << QCoreApplication::applicationVersion() << "\n";
    out << "portable=" << (AppPaths::isPortable() ? "true" : "false") << "\n";
    out << "dataDir=" << AppPaths::dataDir() << "\n";
    out << "config=" << AppPaths::configFile() << "\n";
    out << "log=" << Logger::logFilePath() << "\n\n";

    out << "screens:\n";
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect g = screen->geometry();
        out << "  " << screen->name()
            << " geometry=" << g.x() << "," << g.y() << " " << g.width() << "x" << g.height()
            << " dpr=" << screen->devicePixelRatio()
            << " logicalDpi=" << screen->logicalDotsPerInch()
            << " primary=" << (screen == QGuiApplication::primaryScreen() ? "true" : "false")
            << "\n";
    }

    out << "\nos: " << QSysInfo::productType() << " " << QSysInfo::productVersion()
        << " kernel=" << QSysInfo::kernelVersion() << "\n";
    out << "qt=" << qVersion() << "\n";
    out << "arch=" << QSysInfo::currentCpuArchitecture() << "\n";
    out << "exe=" << QCoreApplication::applicationFilePath() << "\n";
    return s;
}

} // namespace

namespace Diagnostics {

QString exportBundle(const QString &outputDir, QString *error)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString outDir = outputDir.trimmed().isEmpty()
                               ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                               : QDir::cleanPath(outputDir.trimmed());
    if (!QDir().mkpath(outDir)) {
        if (error)
            *error = QStringLiteral("无法创建导出目录：%1").arg(outDir);
        return {};
    }
    const QString zipPath =
        QDir(outDir).filePath(QStringLiteral("FCapture-diag-%1.zip").arg(stamp));

    ZipStoreWriter zip;
    zip.addFile(QStringLiteral("system.txt"), systemSummary().toUtf8());

    // 配置文件原样带上，排查配置类问题最直接
    QFile cfg(AppPaths::configFile());
    if (cfg.open(QIODevice::ReadOnly))
        zip.addFile(QStringLiteral("config.ini"), cfg.readAll());

    addDir(zip, AppPaths::logDir(), QStringLiteral("log"));
    addDir(zip, AppPaths::crashDir(), QStringLiteral("crash"));

    QString err;
    if (!zip.writeTo(zipPath, &err)) {
        if (error)
            *error = err.isEmpty() ? QStringLiteral("无法写入诊断压缩包") : err;
        return {};
    }
    return zipPath;
}

} // namespace Diagnostics
