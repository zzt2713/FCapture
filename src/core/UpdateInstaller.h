#pragma once

#include <QString>

// 应用内更新：把下载好的 zip 覆盖到当前安装目录并重启。
//
// 为什么必须依赖外部脚本：
//   Windows 上正在运行的 exe / dll 无法被覆盖，只能退出本进程后再让外部程序替换。
//   做法是写一个 .bat：
//       等本进程退出 → 备份旧文件 → 解压新包覆盖 → 启动新 exe → 自删
//   然后以隐藏窗口方式启动它，本进程立即退出。
namespace UpdateInstaller {

// 校验能安全执行更新的前提：
//   - 安装目录可写
//   - 找到 7z 或 PowerShell（解压用）
// 失败时通过 error 返回原因。
bool canInstall(QString *error = nullptr);

// 写脚本并启动，随后调用方应立即退出应用。
// zipPath 必须是绝对路径。
bool installAndRestart(const QString &zipPath, QString *error = nullptr);

} // namespace UpdateInstaller
