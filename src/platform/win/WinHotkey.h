#pragma once

#include <QString>
#include <QtGlobal>

// 全局热键的 Win32 封装。HotkeyManager 通过这里间接调用，避免 core/ 层 include <windows.h>
namespace WinHotkey {

enum class RegisterResult {
    Success,
    Occupied,
    Failed,
};

bool parseKeyString(const QString &keyString, quint32 *modifiers, quint32 *vk);

RegisterResult registerHotkey(int id, quint32 modifiers, quint32 vk);
void unregisterHotkey(int id);

// 若是 WM_HOTKEY 消息，返回 true 并写出热键 id
bool handleNativeMessage(void *message, int *outId);

} // namespace WinHotkey
