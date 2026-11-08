#pragma once

#include <QString>

// 崩溃捕获：把未捕获的 SEH 异常 / C++ 异常写进日志文件，
// 同时生成 minidump 和崩溃标记，供下次启动提示用户。
// 头文件不暴露任何 Win32 类型。
namespace WinCrashHandler {

// logPath：崩溃文本追加到该文件
// dumpDir：minidump（.dmp）与崩溃标记 marker 的存放目录
void install(const QString &logPath, const QString &dumpDir);

// 上一次运行是否异常退出（存在崩溃标记）。返回值非空表示有未处理崩溃，
// 内容为崩溃摘要，供启动时提示用户。
QString consumeCrashMarker();

} // namespace WinCrashHandler
