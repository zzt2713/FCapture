#include "platform/win/VirtualDesktop.h"

#include <QMutex>
#include <QMutexLocker>

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

QMutex g_mutex;
ComPtr<IVirtualDesktopManager> g_manager;
bool g_initAttempted = false;

// 只在主线程（QApplication 已 OleInitialize，STA）调用
IVirtualDesktopManager *manager()
{
    QMutexLocker lock(&g_mutex);
    if (g_initAttempted)
        return g_manager.Get();
    g_initAttempted = true;

    ComPtr<IVirtualDesktopManager> mgr;
    if (SUCCEEDED(::CoCreateInstance(__uuidof(VirtualDesktopManager), nullptr,
                                     CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(mgr.GetAddressOf())))) {
        g_manager = std::move(mgr);
    }
    return g_manager.Get();
}

} // namespace

namespace VirtualDesktop {

bool isSupported()
{
    return manager() != nullptr;
}

bool isWindowOnCurrentDesktop(quintptr hwnd)
{
    IVirtualDesktopManager *mgr = manager();
    if (!mgr || !hwnd)
        return true;   // 不支持时视为"始终可见"

    BOOL onCurrent = TRUE;
    if (FAILED(mgr->IsWindowOnCurrentVirtualDesktop(
            reinterpret_cast<HWND>(hwnd), &onCurrent)))
        return true;
    return onCurrent != FALSE;
}

QUuid windowDesktopId(quintptr hwnd)
{
    IVirtualDesktopManager *mgr = manager();
    if (!mgr || !hwnd)
        return {};

    GUID guid{};
    if (FAILED(mgr->GetWindowDesktopId(reinterpret_cast<HWND>(hwnd), &guid)))
        return {};
    return QUuid(guid);
}

QUuid currentDesktopId()
{
    // 公开接口查不到"当前桌面"，借前台窗口反推；拿不到返回空
    const HWND fg = ::GetForegroundWindow();
    if (!fg)
        return {};
    return windowDesktopId(reinterpret_cast<quintptr>(fg));
}

bool moveWindowToDesktop(quintptr hwnd, const QUuid &desktopId)
{
    IVirtualDesktopManager *mgr = manager();
    if (!mgr || !hwnd || desktopId.isNull())
        return false;

    const GUID guid = desktopId;
    return SUCCEEDED(mgr->MoveWindowToDesktop(reinterpret_cast<HWND>(hwnd), guid));
}

} // namespace VirtualDesktop
