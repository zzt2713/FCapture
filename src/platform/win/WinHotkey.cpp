#include "platform/win/WinHotkey.h"

#include <QStringList>

#include <windows.h>

namespace WinHotkey {

bool parseKeyString(const QString &keyString, quint32 *modifiers, quint32 *vk)
{
    if (!modifiers || !vk)
        return false;

    quint32 mods = 0;
    quint32 key = 0;

    const QStringList parts = keyString.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return false;

    for (int i = 0; i < parts.size(); ++i) {
        const QString t = parts.at(i).trimmed();
        if (i < parts.size() - 1) {
            // 修饰键
            const QString lower = t.toLower();
            if (lower == QStringLiteral("ctrl") || lower == QStringLiteral("control"))
                mods |= MOD_CONTROL;
            else if (lower == QStringLiteral("alt"))
                mods |= MOD_ALT;
            else if (lower == QStringLiteral("shift"))
                mods |= MOD_SHIFT;
            else if (lower == QStringLiteral("win"))
                mods |= MOD_WIN;
            else
                return false;
            continue;
        }

        // 主键
        if (t.size() == 1 && t.at(0).isLetterOrNumber()) {
            key = static_cast<quint32>(t.at(0).toUpper().unicode());
        } else if (t.size() == 1 && t.at(0).isPunct()) {
            const QChar c = t.at(0);
            switch (c.unicode()) {
            case '-': key = VK_OEM_MINUS; break;
            case '=': key = VK_OEM_PLUS; break;
            case '[': key = VK_OEM_4; break;
            case ']': key = VK_OEM_6; break;
            case ';': key = VK_OEM_1; break;
            case '\'': key = VK_OEM_7; break;
            case ',': key = VK_OEM_COMMA; break;
            case '.': key = VK_OEM_PERIOD; break;
            case '/': key = VK_OEM_2; break;
            case '\\': key = VK_OEM_5; break;
            case '`': key = VK_OEM_3; break;
            default: return false;
            }
        } else {
            const QString upper = t.toUpper();
            if (upper.startsWith(QLatin1Char('F'))) {
                bool ok = false;
                const int n = upper.mid(1).toInt(&ok);
                if (!ok || n < 1 || n > 24)
                    return false;
                key = static_cast<quint32>(VK_F1 + n - 1);
            } else if (upper == QStringLiteral("PRINT") || upper == QStringLiteral("PRINTSCREEN")) {
                key = VK_SNAPSHOT;
            } else if (upper == QStringLiteral("INSERT")) {
                key = VK_INSERT;
            } else if (upper == QStringLiteral("DELETE")) {
                key = VK_DELETE;
            } else if (upper == QStringLiteral("HOME")) {
                key = VK_HOME;
            } else if (upper == QStringLiteral("END")) {
                key = VK_END;
            } else if (upper == QStringLiteral("PAGEUP")) {
                key = VK_PRIOR;
            } else if (upper == QStringLiteral("PAGEDOWN")) {
                key = VK_NEXT;
            } else if (upper == QStringLiteral("SPACE")) {
                key = VK_SPACE;
            } else if (upper == QStringLiteral("TAB")) {
                key = VK_TAB;
            } else if (upper == QStringLiteral("ENTER") || upper == QStringLiteral("RETURN")) {
                key = VK_RETURN;
            } else if (upper == QStringLiteral("BACKSPACE")) {
                key = VK_BACK;
            } else if (upper == QStringLiteral("LEFT") || upper == QStringLiteral("ARROWLEFT")) {
                key = VK_LEFT;
            } else if (upper == QStringLiteral("RIGHT") || upper == QStringLiteral("ARROWRIGHT")) {
                key = VK_RIGHT;
            } else if (upper == QStringLiteral("UP") || upper == QStringLiteral("ARROWUP")) {
                key = VK_UP;
            } else if (upper == QStringLiteral("DOWN") || upper == QStringLiteral("ARROWDOWN")) {
                key = VK_DOWN;
            } else if (upper == QStringLiteral("ESC") || upper == QStringLiteral("ESCAPE")) {
                key = VK_ESCAPE;
            } else {
                return false;
            }
        }
    }

    if (key == 0)
        return false;

    *modifiers = mods;
    *vk = key;
    return true;
}

RegisterResult registerHotkey(int id, quint32 modifiers, quint32 vk)
{
    if (::RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk))
        return RegisterResult::Success;

    return ::GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED
               ? RegisterResult::Occupied
               : RegisterResult::Failed;
}

void unregisterHotkey(int id)
{
    ::UnregisterHotKey(nullptr, id);
}

bool handleNativeMessage(void *message, int *outId)
{
    if (!message || !outId)
        return false;
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_HOTKEY)
        return false;
    *outId = static_cast<int>(msg->wParam);
    return true;
}

} // namespace WinHotkey
