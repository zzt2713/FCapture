#include "core/Logger.h"

#include "core/AppPaths.h"
#include "platform/win/WinCrashHandler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QTextStream>

#include <cstdio>

namespace {

constexpr qint64 kMaxLogSize = 2 * 1024 * 1024;
constexpr int kMaxRotations = 3;      // fcapture.log / .1 / .2 / .3
constexpr int kMaxCrashDumps = 5;     // 崩溃转储只保留最近 5 个

QMutex g_mutex;
QString g_logPath;
QFile g_file;
QTextStream g_stream;
// 默认只写 INFO 及以上；--log-level=debug 时放开 DEBUG
bool g_debugEnabled = false;

void rotateIfNeeded()
{
    if (!g_file.isOpen())
        return;
    if (g_file.size() < kMaxLogSize)
        return;

    g_stream.flush();
    g_file.close();

    for (int i = kMaxRotations - 1; i >= 1; --i) {
        const QString from = QStringLiteral("%1.%2").arg(g_logPath).arg(i);
        const QString to = QStringLiteral("%1.%2").arg(g_logPath).arg(i + 1);
        if (QFile::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    const QString first = g_logPath + QStringLiteral(".1");
    QFile::remove(first);
    QFile::rename(g_logPath, first);

    g_file.setFileName(g_logPath);
    g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    g_stream.setDevice(&g_file);
}

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO";
    case QtWarningMsg:  return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "LOG";
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // DEBUG 默认丢弃，避免日志被刷爆；--log-level=debug 时才写入
    if (type == QtDebugMsg && !g_debugEnabled) {
        std::fprintf(stderr, "[DEBUG] %s\n", msg.toLocal8Bit().constData());
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString file = context.file ? QFileInfo(QString::fromUtf8(context.file)).fileName() : QStringLiteral("?");
    const QString line = QStringLiteral("[%1] [%2] %3:%4 — %5")
                             .arg(stamp, QString::fromLatin1(levelName(type)), file)
                             .arg(context.line)
                             .arg(msg);

    {
        QMutexLocker locker(&g_mutex);
        if (g_file.isOpen()) {
            rotateIfNeeded();
            g_stream << line << '\n';
            g_stream.flush();
        }
    }

    // 调试时同时输出到控制台
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());

    if (type == QtFatalMsg)
        std::abort();
}

} // namespace

void Logger::init(bool debug)
{
    g_debugEnabled = debug;

    const QString dir = AppPaths::logDir();
    QDir().mkpath(dir);
    g_logPath = dir + QStringLiteral("/fcapture.log");

    g_file.setFileName(g_logPath);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "Logger: cannot open %s\n", g_logPath.toLocal8Bit().constData());
        return;
    }
    g_stream.setDevice(&g_file);
    g_stream.setEncoding(QStringConverter::Utf8);

    qInstallMessageHandler(messageHandler);
}

namespace {

// 崩溃转储保留最近 kMaxCrashDumps 个。
// minidump 单个约 1.8MB，且只在崩溃时写入；不清理的话长期使用会累积到几十上百 MB。
void pruneCrashDumps(const QString &dir, int keep)
{
    QDir crashDir(dir);
    if (!crashDir.exists())
        return;

    QFileInfoList dumps = crashDir.entryInfoList(
        {QStringLiteral("crash-*.dmp")}, QDir::Files, QDir::Time | QDir::Reversed);
    while (dumps.size() > keep) {
        const QFileInfo oldest = dumps.takeFirst();
        QFile::remove(oldest.absoluteFilePath());
    }
}

} // namespace

void Logger::installCrashHandler()
{
    // 崩溃日志单独一份：主日志由 Logger 长期持有，
    // 崩溃时再写同一文件容易受锁/缓冲影响
    const QString dir = AppPaths::logDir();
    WinCrashHandler::install(dir + QStringLiteral("/crash.log"), AppPaths::crashDir());
    pruneCrashDumps(AppPaths::crashDir(), kMaxCrashDumps);
}

QString Logger::logFilePath()
{
    return g_logPath;
}
