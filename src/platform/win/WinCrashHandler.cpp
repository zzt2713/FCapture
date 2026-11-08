#include "platform/win/WinCrashHandler.h"

#include <QFile>

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <string>

#pragma comment(lib, "dbghelp.lib")

namespace {

// 崩溃处理期间必须保持可用的数据：用固定缓冲，
// 避免在崩溃现场再做一次堆分配。
wchar_t g_logPath[MAX_PATH] = {};
wchar_t g_dumpDir[MAX_PATH] = {};

// 崩溃标记文件名：存在即代表上次是异常退出
constexpr const wchar_t *kMarkerName = L"last-crash.txt";

// 把宽字符串复制进固定缓冲，超长截断（崩溃现场不能抛异常）
void copyToBuffer(wchar_t *dst, size_t capacity, const std::wstring &src)
{
    const size_t maxLen = capacity - 1;
    const size_t len = src.size() < maxLen ? src.size() : maxLen;
    std::wmemcpy(dst, src.c_str(), len);
    dst[len] = L'\0';
}

// 生成 minidump 到 dump 目录，返回写出的文件路径（失败返回空）
std::wstring writeMiniDump(EXCEPTION_POINTERS *info)
{
    if (g_dumpDir[0] == L'\0')
        return {};

    // 目录可能不存在：崩溃现场仍然允许 CreateDirectory（不涉及 CRT 堆）
    CreateDirectoryW(g_dumpDir, nullptr);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[64];
    std::swprintf(name, 64, L"crash-%04u%02u%02u-%02u%02u%02u.dmp",
                  st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    wchar_t path[MAX_PATH];
    std::swprintf(path, MAX_PATH, L"%s\\%s", g_dumpDir, name);

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    MINIDUMP_EXCEPTION_INFORMATION exInfo{};
    exInfo.ThreadId = GetCurrentThreadId();
    exInfo.ExceptionPointers = info;
    exInfo.ClientPointers = FALSE;

    // MiniDumpWithIndirectlyReferencedMemory：能带上一部分堆内容，
    // 体积可控且足够定位 Qt 对象里的崩溃
    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo
        | MiniDumpWithIndirectlyReferencedMemory | MiniDumpNormal);

    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                      file, type, info ? &exInfo : nullptr, nullptr, nullptr);
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path);
        return {};
    }
    return path;
}

// 写崩溃标记：下次启动据此提示用户
void writeCrashMarker(const wchar_t *dumpPath, const char *detail)
{
    if (g_dumpDir[0] == L'\0')
        return;

    wchar_t path[MAX_PATH];
    std::swprintf(path, MAX_PATH, L"%s\\%s", g_dumpDir, kMarkerName);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    char buf[1024];
    std::snprintf(buf, sizeof(buf), "dump=%ls\n%s\n",
                  dumpPath && dumpPath[0] ? dumpPath : L"(minidump failed)",
                  detail ? detail : "");
    DWORD written = 0;
    WriteFile(h, buf, DWORD(std::strlen(buf)), &written, nullptr);
    CloseHandle(h);
}

void appendToLog(const char *text)
{
    if (g_logPath[0] == L'\0' || text == nullptr)
        return;

    // 用宽字符路径：中文用户名目录在 CreateFileA 下会打不开。
    // 允许共享读写：崩溃时主日志可能仍被 Logger 持有。
    HANDLE h = CreateFileW(g_logPath, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(h, text, DWORD(std::strlen(text)), &written, nullptr);
    CloseHandle(h);
}

void appendLine(const char *prefix, const char *detail)
{
    char buf[2048];
    std::snprintf(buf, sizeof(buf), "[CRASH] %s%s\n", prefix, detail ? detail : "");
    appendToLog(buf);
}

// 把调用栈符号化后写入日志
void dumpStack(CONTEXT *context)
{
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    if (!SymInitialize(process, nullptr, TRUE))
        return;

    STACKFRAME64 frame{};
    DWORD machine = 0;

#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context->Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#endif

    std::string out = "[CRASH] ---- call stack ----\n";

    for (int i = 0; i < 64; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0)
            break;

        char line[1024];
        DWORD64 displacement = 0;

        char symbolBuf[sizeof(SYMBOL_INFO) + 256] = {};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuf);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        const DWORD64 address = frame.AddrPC.Offset;

        if (SymFromAddr(process, address, &displacement, symbol)) {
            IMAGEHLP_LINE64 lineInfo{};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp = 0;
            if (SymGetLineFromAddr64(process, address, &lineDisp, &lineInfo)) {
                std::snprintf(line, sizeof(line), "[CRASH]   #%02d %s+0x%llx  (%s:%lu)\n", i,
                              symbol->Name,
                              static_cast<unsigned long long>(displacement),
                              lineInfo.FileName ? lineInfo.FileName : "?",
                              lineInfo.LineNumber);
            } else {
                std::snprintf(line, sizeof(line), "[CRASH]   #%02d %s+0x%llx\n", i, symbol->Name,
                              static_cast<unsigned long long>(displacement));
            }
        } else {
            std::snprintf(line, sizeof(line), "[CRASH]   #%02d 0x%llx\n", i,
                          static_cast<unsigned long long>(address));
        }
        out += line;
    }

    out += "[CRASH] ---- end of stack ----\n";
    appendToLog(out.c_str());

    SymCleanup(process);
}

LONG WINAPI sehHandler(EXCEPTION_POINTERS *info)
{
    char detail[512] = {};
    if (info && info->ExceptionRecord) {
        std::snprintf(detail, sizeof(detail), "code=0x%08lX address=%p",
                      info->ExceptionRecord->ExceptionCode,
                      info->ExceptionRecord->ExceptionAddress);
        appendLine("unhandled SEH exception: ", detail);
    }

    // 先出 minidump：栈符号化耗时较长，万一中途再崩，dump 已经落盘
    const std::wstring dump = writeMiniDump(info);
    if (!dump.empty()) {
        char note[512];
        std::snprintf(note, sizeof(note), " minidump=%ls",
                      dump.c_str());   // %ls：wchar_t 路径，中文目录也正确
        appendToLog("[CRASH] minidump written:");
        appendToLog(note);
    }

    if (info && info->ContextRecord)
        dumpStack(info->ContextRecord);

    writeCrashMarker(dump.c_str(), detail);
    appendToLog("[CRASH] process is terminating\n");
    return EXCEPTION_EXECUTE_HANDLER;
}

void terminateHandler()
{
    appendLine("std::terminate called", nullptr);
    writeCrashMarker(L"", "std::terminate");
    std::abort();
}

} // namespace

namespace WinCrashHandler {

void install(const QString &logPath, const QString &dumpDir)
{
    copyToBuffer(g_logPath, sizeof(g_logPath) / sizeof(wchar_t), logPath.toStdWString());
    copyToBuffer(g_dumpDir, sizeof(g_dumpDir) / sizeof(wchar_t), dumpDir.toStdWString());

    SetUnhandledExceptionFilter(sehHandler);
    std::set_terminate(terminateHandler);

    appendToLog("[CRASH] crash handler installed\n");
}

QString consumeCrashMarker()
{
    if (g_dumpDir[0] == L'\0')
        return {};

    wchar_t path[MAX_PATH];
    std::swprintf(path, MAX_PATH, L"%s\\%s", g_dumpDir, kMarkerName);

    QFile marker(QString::fromWCharArray(path));
    if (!marker.exists())
        return {};

    if (!marker.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(marker.readAll()).trimmed();
    marker.close();

    // 读一次就删：避免每次启动都重复弹同一个崩溃
    QFile::remove(QString::fromWCharArray(path));
    return text;
}

} // namespace WinCrashHandler
