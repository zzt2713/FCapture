#pragma once

#include <QString>

// 导出诊断信息：把日志、崩溃转储、配置、系统信息打成一个 zip，
// 方便用户反馈问题时一次性提供全部现场。
namespace Diagnostics {

// 生成诊断 zip 到 outputDir（为空时使用桌面），返回完整路径；
// 失败返回空字符串并通过 error 给出原因。
QString exportBundle(const QString &outputDir = QString(), QString *error = nullptr);

} // namespace Diagnostics
