#include "core/HotkeyManager.h"
#include "platform/win/WinHotkey.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

bool sameKey(const QString &left, const QString &right)
{
    quint32 leftModifiers = 0;
    quint32 leftVirtualKey = 0;
    quint32 rightModifiers = 0;
    quint32 rightVirtualKey = 0;
    return HotkeyManager::parseKey(left, &leftModifiers, &leftVirtualKey)
           && HotkeyManager::parseKey(right, &rightModifiers, &rightVirtualKey)
           && leftModifiers == rightModifiers
           && leftVirtualKey == rightVirtualKey;
}

} // namespace

HotkeyManager::HotkeyManager(QObject *parent)
    : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool HotkeyManager::parseKey(const QString &keyString, quint32 *modifiers, quint32 *vk)
{
    return WinHotkey::parseKeyString(keyString, modifiers, vk);
}

HotkeyManager::RegistrationStatus HotkeyManager::registerHotkey(
    const QString &action, const QString &keyString)
{
    quint32 mods = 0;
    quint32 vk = 0;
    if (!parseKey(keyString, &mods, &vk)) {
        qWarning() << "HotkeyManager: invalid key string" << keyString << "for action" << action;
        return RegistrationStatus::Invalid;
    }

    auto it = m_byAction.find(action);
    if (!m_suspended && it != m_byAction.end() && it->id > 0
        && sameKey(it->keyString, keyString)) {
        return RegistrationStatus::Success;
    }

    Binding b;
    b.action = action;
    b.keyString = keyString;

    const int id = m_nextId++;
    const WinHotkey::RegisterResult result = WinHotkey::registerHotkey(id, mods, vk);
    if (result != WinHotkey::RegisterResult::Success) {
        qWarning() << "HotkeyManager: RegisterHotKey failed for" << keyString << "action" << action;
        return result == WinHotkey::RegisterResult::Occupied
                   ? RegistrationStatus::Occupied
                   : RegistrationStatus::Failed;
    }

    if (m_suspended) {
        // Probe while recording, then release the key until resume().
        WinHotkey::unregisterHotkey(id);
        m_byAction.insert(action, b);
        return RegistrationStatus::Success;
    }

    if (it != m_byAction.end() && it->id > 0) {
        WinHotkey::unregisterHotkey(it->id);
        m_byId.remove(it->id);
    }

    b.id = id;
    m_byAction.insert(action, b);
    m_byId.insert(id, action);
    return RegistrationStatus::Success;
}

void HotkeyManager::suspend()
{
    if (m_suspended)
        return;

    unregisterBindings();
    m_suspended = true;
}

QStringList HotkeyManager::resume()
{
    QStringList failed;
    if (!m_suspended)
        return failed;

    m_suspended = false;
    for (auto it = m_byAction.begin(); it != m_byAction.end(); ++it) {
        quint32 mods = 0;
        quint32 vk = 0;
        if (!parseKey(it->keyString, &mods, &vk)) {
            qWarning() << "HotkeyManager: cannot restore invalid hotkey"
                       << it->keyString << "for action" << it->action;
            failed << it->action;
            continue;
        }

        const int id = m_nextId++;
        if (WinHotkey::registerHotkey(id, mods, vk)
            != WinHotkey::RegisterResult::Success) {
            qWarning() << "HotkeyManager: failed to restore hotkey"
                       << it->keyString << "for action" << it->action;
            failed << it->action;
            continue;
        }

        it->id = id;
        m_byId.insert(id, it->action);
    }
    return failed;
}

bool HotkeyManager::isSuspended() const
{
    return m_suspended;
}

void HotkeyManager::unregisterAll()
{
    unregisterBindings();
    m_byAction.clear();
    m_suspended = false;
}

void HotkeyManager::unregisterBindings()
{
    for (auto it = m_byAction.begin(); it != m_byAction.end(); ++it) {
        if (it->id > 0)
            WinHotkey::unregisterHotkey(it->id);
        it->id = 0;
    }
    m_byId.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    int id = 0;
    if (!WinHotkey::handleNativeMessage(message, &id))
        return false;

    const QString action = m_byId.value(id);
    if (action.isEmpty())
        return false;

    emit hotkeyPressed(action);
    return true;
}
