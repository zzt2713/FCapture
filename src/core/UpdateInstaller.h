#pragma once

#include <QString>

// 应用内更新：把下载好的 zip 覆盖到当前安装目录并重启。
//
// 为什么必须依赖外部脚本：
//   Windows 上正在运行的 exe / dll 无法被覆盖，只能退出本进程后再让外部程序替换。
//   做法是写一个 .bat：
//       等本进程退出 → 备份旧文件 → 解压新包覆盖 → 同步卸载注册表版本号
//       → 启动新 exe → 自删
//   脚本以管理员权限运行并弹一次 UAC：覆盖 Program Files、写 HKLM 注册表
//   （Windows 设置→应用 显示的版本号）都需要。用户拒绝 UAC 则不更新、应用不退出。
namespace UpdateInstaller {

// 写脚本并以管理员权限启动它，随后调用方应立即退出应用。
// zipPath 必须是绝对路径；version 是新版本号（如 "1.1.0"），用于同步
// 卸载注册表条目的 DisplayVersion。
bool installAndRestart(const QString &zipPath, const QString &version,
                       QString *error = nullptr);

} // namespace UpdateInstaller
