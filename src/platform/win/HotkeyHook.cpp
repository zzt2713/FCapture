#include "platform/win/HotkeyHook.h"

#include <QDebug>

#include <windows.h>

MouseHook *MouseHook::s_instance = nullptr;

MouseHook::MouseHook(QObject *parent)
    : QObject(parent)
{
}

MouseHook::~MouseHook()
{
    uninstall();
}

bool MouseHook::install()
{
    if (m_installed)
        return true;

    s_instance = this;
    // WH_MOUSE_LL 不需要 DLL 注入，可以传模块句柄为 null
    m_hook = ::SetWindowsHookExW(WH_MOUSE_LL,
                                 reinterpret_cast<HOOKPROC>(&MouseHook::hookProc),
                                 nullptr, 0);
    m_installed = (m_hook != nullptr);
    if (!m_installed) {
        qWarning() << "MouseHook: SetWindowsHookExW failed:" << ::GetLastError();
        s_instance = nullptr;
    }
    return m_installed;
}

void MouseHook::uninstall()
{
    if (m_hook) {
        ::UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_hook));
        m_hook = nullptr;
    }
    m_installed = false;
    m_dragging = false;
    if (s_instance == this)
        s_instance = nullptr;
}

long long __stdcall MouseHook::hookProc(int code, unsigned long long wParam, long long lParam)
{
    // 回调必须尽快返回
    if (code < 0 || !s_instance)
        return ::CallNextHookEx(nullptr, code, WPARAM(wParam), LPARAM(lParam));

    auto *info = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
    if (!info)
        return ::CallNextHookEx(nullptr, code, WPARAM(wParam), LPARAM(lParam));

    const bool winDown = (::GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
                         || (::GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    if (!winDown) {
        if (s_instance->m_dragging) {
            // Win 中途松开：放弃这次超级截图
            s_instance->m_dragging = false;
        }
        return ::CallNextHookEx(nullptr, code, WPARAM(wParam), LPARAM(lParam));
    }

    switch (wParam) {
    case WM_LBUTTONDOWN:
        s_instance->m_dragging = true;
        s_instance->m_start = QPoint(info->pt.x, info->pt.y);
        break;

    case WM_MOUSEMOVE:
        // 移动事件只记录，不做任何重活
        break;

    case WM_LBUTTONUP:
        if (s_instance->m_dragging) {
            s_instance->m_dragging = false;
            const QPoint from = s_instance->m_start;
            const QPoint to(info->pt.x, info->pt.y);
            // 位移太小当误触，忽略
            if ((to - from).manhattanLength() >= 8)
                emit s_instance->superSnipFinished(from, to);
        }
        break;

    default:
        break;
    }

    return ::CallNextHookEx(nullptr, code, WPARAM(wParam), LPARAM(lParam));
}

// ---------------------------------------------------------------- KeyboardHook

KeyboardHook *KeyboardHook::s_instance = nullptr;

KeyboardHook::KeyboardHook(QObject *parent)
    : QObject(parent)
{
}

KeyboardHook::~KeyboardHook()
{
    uninstall();
}

bool KeyboardHook::install()
{
    if (m_installed)
        return true;

    s_instance = this;
    m_hook = ::SetWindowsHookExW(WH_KEYBOARD_LL,
                                 reinterpret_cast<HOOKPROC>(&KeyboardHook::hookProc),
                                 nullptr, 0);
    m_installed = (m_hook != nullptr);
    if (!m_installed) {
        qWarning() << "KeyboardHook: SetWindowsHookExW failed:" << ::GetLastError();
        s_instance = nullptr;
    }
    return m_installed;
}

void KeyboardHook::uninstall()
{
    if (m_hook) {
        ::UnhookWindowsHookEx(reinterpret_cast<HHOOK>(m_hook));
        m_hook = nullptr;
    }
    m_installed = false;
    if (s_instance == this)
        s_instance = nullptr;
}

long long __stdcall KeyboardHook::hookProc(int code, unsigned long long wParam,
                                           long long lParam)
{
    // 回调里不做任何重活（< 30ms 返回），只发信号排队到主线程
    if (code >= 0 && wParam == WM_KEYDOWN && s_instance) {
        const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (kb && kb->vkCode == VK_ESCAPE)
            emit s_instance->escapePressed();
    }
    return ::CallNextHookEx(nullptr, code, static_cast<WPARAM>(wParam),
                            static_cast<LPARAM>(lParam));
}
