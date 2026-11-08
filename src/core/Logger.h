#pragma once

#include <QString>

class Logger
{
public:
    // debug=true 时 qDebug 也落盘（由 --log-level=debug 触发）
    static void init(bool debug = false);
    // 安装崩溃处理器：未捕获异常 / 访问冲突时把调用栈写进日志
    static void installCrashHandler();
    static QString logFilePath();

private:
    Logger() = delete;
};
