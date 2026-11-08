#pragma once

#include <QString>

// 应用路径解析：统一决定配置、日志、崩溃转储、历史缓存落在哪里。
// 便携模式（exe 同目录有 portable.txt）时全部落在 exe 同目录，
// 否则落在用户 AppData 下。所有模块都必须从这里取路径，不要各自拼。
namespace AppPaths {

// 是否便携模式：exe 同目录存在 portable.txt
bool isPortable();

// 应用根数据目录：
//   便携模式 → <exe 目录>/data
//   普通模式 → %LOCALAPPDATA%/FCapture
QString dataDir();

// 配置文件：<dataDir>/config.ini
QString configFile();

// 日志目录：<dataDir>/log
QString logDir();

// 崩溃转储目录：<dataDir>/crash
QString crashDir();

// exe 所在目录（不带末尾斜杠）
QString exeDir();

// 首次运行（新数据目录还不存在）时，把旧版 SnipasteClone 的数据目录
// 整体复制过来，避免改名后用户配置/日志丢失。
// 只在普通模式下做；便携模式的数据就在 exe 旁边，用户自己拷走即可。
void migrateLegacyDataIfNeeded();

} // namespace AppPaths
