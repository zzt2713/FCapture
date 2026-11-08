#include "platform/win/AppLauncher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace {

void collectFromRoot(const QString &root, QVector<InstalledApp> *out, QSet<QString> *seen)
{
    QSettings reg(root, QSettings::NativeFormat);
    const QStringList exes = reg.childGroups();
    for (const QString &exe : exes) {
        const QString path = reg.value(exe + QStringLiteral("/Default")).toString();
        if (path.isEmpty())
            continue;
        const QFileInfo fi(path);
        if (!fi.isFile())
            continue;

        const QString key = fi.absoluteFilePath().toLower();
        if (seen->contains(key))
            continue;
        seen->insert(key);

        InstalledApp app;
        app.executablePath = fi.absoluteFilePath();
        app.name = fi.completeBaseName();
        out->append(app);
    }
}

} // namespace

namespace AppLauncher {

QVector<InstalledApp> installedApps()
{
    QVector<InstalledApp> apps;
    QSet<QString> seen;

    collectFromRoot(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
                    &apps, &seen);
    collectFromRoot(QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths"),
                    &apps, &seen);

    // App Paths 里混着大量系统工具，只保留常见的图像/浏览器/编辑器类应用
    static const QStringList kInteresting = {
        QStringLiteral("photos"), QStringLiteral("paint"), QStringLiteral("mspaint"),
        QStringLiteral("chrome"), QStringLiteral("msedge"), QStringLiteral("firefox"),
        QStringLiteral("code"), QStringLiteral("notepad"), QStringLiteral("notepad++"),
        QStringLiteral("wordpad"), QStringLiteral("snipaste"), QStringLiteral("explorer"),
        QStringLiteral("wxwork"), QStringLiteral("qq"), QStringLiteral("wechat"),
        QStringLiteral("telegram"), QStringLiteral("slack"), QStringLiteral("discord"),
        QStringLiteral("acrobat"), QStringLiteral("photoshop"), QStringLiteral("excel"),
        QStringLiteral("winword"), QStringLiteral("powerpnt"),
    };

    QVector<InstalledApp> filtered;
    for (const InstalledApp &app : apps) {
        const QString lower = app.name.toLower();
        for (const QString &key : kInteresting) {
            if (lower.contains(key)) {
                filtered.append(app);
                break;
            }
        }
    }

    std::sort(filtered.begin(), filtered.end(),
              [](const InstalledApp &a, const InstalledApp &b) {
                  return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
              });
    return filtered;
}

bool openWith(const QString &executablePath, const QString &filePath)
{
    if (executablePath.isEmpty() || filePath.isEmpty())
        return false;
    if (!QFileInfo::exists(executablePath))
        return false;
    if (!QFileInfo::exists(filePath))
        return false;

    return QProcess::startDetached(executablePath,
                                   {QDir::toNativeSeparators(filePath)});
}

} // namespace AppLauncher
