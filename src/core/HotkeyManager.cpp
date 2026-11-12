#include "core/HotkeyManager.h"
#include "platform/win/FullscreenDetector.h"
#include "platform/win/WinHotkey.h"

#include <QCoreApplication>
#include <QDateTime>
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
        m_failedKeys[action].append(keyString);
        return RegistrationStatus::Invalid;
    }

    QVector<Binding> &bindings = m_byAction[action];

    // 该动作已经绑了同一个键 → 幂等返回成功
    for (const Binding &existing : bindings) {
        if (existing.id > 0 && sameKey(existing.keyString, keyString)) {
            m_failedKeys[action].removeAll(keyString);
            return RegistrationStatus::Success;
        }
    }

    Binding b;
    b.action = action;
    b.keyString = keyString;

    const int id = m_nextId++;
    const WinHotkey::RegisterResult result = WinHotkey::registerHotkey(id, mods, vk);
    if (result != WinHotkey::RegisterResult::Success) {
        qWarning() << "HotkeyManager: RegisterHotKey failed for" << keyString << "action" << action;
        m_failedKeys[action].append(keyString);
        if (bindings.isEmpty())
            m_byAction.remove(action);
        return result == WinHotkey::RegisterResult::Occupied
                   ? RegistrationStatus::Occupied
                   : RegistrationStatus::Failed;
    }

    m_failedKeys[action].removeAll(keyString);

    b.id = id;
    bindings.append(b);
    m_byId.insert(id, action);
    return RegistrationStatus::Success;
}

void HotkeyManager::unregisterAction(const QString &action)
{
    // 换键前先清旧绑定：旧的失败记录也要一起清，否则重新注册成功后
    // 界面上那个键仍然是红的。
    m_failedKeys.remove(action);

    auto it = m_byAction.find(action);
    if (it == m_byAction.end())
        return;

    for (const Binding &b : *it) {
        if (b.id > 0) {
            WinHotkey::unregisterHotkey(b.id);
            m_byId.remove(b.id);
        }
    }
    m_byAction.erase(it);
}

QStringList HotkeyManager::failedKeys(const QString &action) const
{
    QStringList out;
    for (const QString &key : m_failedKeys.value(action)) {
        if (!out.contains(key, Qt::CaseInsensitive))
            out.append(key);
    }
    return out;
}

QSet<QString> HotkeyManager::conflictedActions() const
{
    QSet<QString> out;
    for (auto it = m_failedKeys.constBegin(); it != m_failedKeys.constEnd(); ++it) {
        if (!it.value().isEmpty())
            out.insert(it.key());
    }
    return out;
}

void HotkeyManager::unregisterAll()
{
    unregisterBindings();
    m_byAction.clear();
    m_failedKeys.clear();
}

void HotkeyManager::unregisterBindings()
{
    for (auto it = m_byAction.begin(); it != m_byAction.end(); ++it) {
        for (Binding &b : *it) {
            if (b.id > 0)
                WinHotkey::unregisterHotkey(b.id);
            b.id = 0;
        }
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

    // 全屏**游戏**前台时屏蔽：热键被我们占用但不响应，避免抢游戏按键。
    // 只看游戏、不看全屏视频/演示（对齐 snow 的 game_windows 默认过滤）。
    // 检测结果缓存 500ms —— WM_HOTKEY 回调里做跨进程查询（OpenProcess/
    // DWM/注册表）会拖慢输入，不能每次都查。
    if (m_blockOnFullscreen) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastFullscreenCheckMs > 500) {
            m_lastFullscreen = FullscreenDetector::focusedFullscreenGameWindowExists();
            m_lastFullscreenCheckMs = now;
        }
        if (m_lastFullscreen)
            return true;   // 吞掉，不触发动作
    }

    // 关键：不能在原生事件过滤器里同步触发动作。
    // WM_HOTKEY 是在 Windows 消息循环内部派发的，同步调用会直接进到
    // startCapture() -> captureCommon() -> QApplication::processEvents()，
    // 也就是在消息循环里重入事件循环 —— Qt 明确禁止，实测表现为
    // "按新快捷键后程序卡死、截图层卡在屏幕上点不动"。
    // 改成排队：等当前消息处理完、回到主事件循环再执行动作。
    QMetaObject::invokeMethod(
        this, [this, action]() { emit hotkeyPressed(action); }, Qt::QueuedConnection);
    return true;
}

void HotkeyManager::setBlockOnFullscreen(bool enabled)
{
    m_blockOnFullscreen = enabled;
    m_lastFullscreenCheckMs = 0;   // 立即失效，下次按键重新判定
}
