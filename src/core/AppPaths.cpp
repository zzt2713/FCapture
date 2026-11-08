#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

bool g_resolved = false;
bool g_portable = false;

} // namespace

namespace AppPaths {

QString exeDir()
{
    return QCoreApplication::applicationDirPath();
}

bool isPortable()
{
    if (!g_resolved) {
        g_resolved = true;
        g_portable = QFile::exists(exeDir() + QStringLiteral("/portable.txt"));
    }
    return g_portable;
}

QString dataDir()
{
    // 便携模式：exe 同目录下的 data/，拷走整个文件夹即带走全部配置
    if (isPortable())
        return exeDir() + QStringLiteral("/data");

    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/F-Capture");
}

QString configFile()
{
    return dataDir() + QStringLiteral("/config.ini");
}

QString logDir()
{
    return dataDir() + QStringLiteral("/log");
}

QString crashDir()
{
    return dataDir() + QStringLiteral("/crash");
}

void migrateLegacyDataIfNeeded()
{
    if (isPortable())
        return;

    const QString target = dataDir();
    if (QDir(target).exists())
        return;   // 新目录已存在，说明迁移过或本来就用新名字

    const QString legacy = QStandardPaths::writableLocation(
                               QStandardPaths::GenericDataLocation)
                           + QStringLiteral("/SnipasteClone");
    if (!QDir(legacy).exists())
        return;

    // 目录本身不存在；逐个复制文件，失败不影响启动
    QDir().mkpath(target);
    const QFileInfoList entries =
        QDir(legacy).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString dst = QDir(target).filePath(entry.fileName());
        if (entry.isDir()) {
            QDir().mkpath(dst);
            const QFileInfoList files =
                QDir(entry.absoluteFilePath()).entryInfoList(QDir::Files);
            for (const QFileInfo &file : files)
                QFile::copy(file.absoluteFilePath(),
                            QDir(dst).filePath(file.fileName()));
        } else {
            QFile::copy(entry.absoluteFilePath(), dst);
        }
    }
}

} // namespace AppPaths
