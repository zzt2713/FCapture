#pragma once

#include <QString>
#include <QVector>

struct InstalledApp
{
    QString name;
    QString executablePath;
};

// 已安装应用列表（读注册表 App Paths）+ 用外部程序打开图片
namespace AppLauncher {

// 扫描 HKCU/HKLM 的 App Paths，返回去重后的应用列表
QVector<InstalledApp> installedApps();

// 把文件路径作为参数启动应用；失败返回 false
bool openWith(const QString &executablePath, const QString &filePath);

} // namespace AppLauncher
