#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

// 全局热键：Win32 RegisterHotKey + QAbstractNativeEventFilter 接收 WM_HOTKEY
class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    enum class RegistrationStatus {
        Success,
        Invalid,
        Occupied,
        Failed,
    };

    explicit HotkeyManager(QObject *parent = nullptr);
    ~HotkeyManager() override;

    // 注册；keyString 形如 "F1" / "Ctrl+Shift+S"。返回注册结果
    RegistrationStatus registerHotkey(const QString &action, const QString &keyString);
    void unregisterAll();

    // 快捷键录制期间临时注销全局热键，结束后按原配置恢复。
    void suspend();
    QStringList resume();
    bool isSuspended() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    // 判断键名字符串（"F1" / "Ctrl+Shift+A"）能否被全局热键解析。
    // 首选项的热键录制器用它做校验，避免把解析不了的组合写进配置。
    static bool parseKey(const QString &keyString, quint32 *modifiers, quint32 *vk);

signals:
    void hotkeyPressed(const QString &action);

private:
    struct Binding
    {
        int     id = 0;
        QString action;
        QString keyString;
    };

    void unregisterBindings();

    int m_nextId = 0x1000;
    QHash<QString, Binding> m_byAction;
    QHash<int, QString>     m_byId;
    bool m_suspended = false;
};
