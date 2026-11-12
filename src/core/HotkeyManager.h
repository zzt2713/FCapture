#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

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

    // 注册；keyString 形如 "F1" / "Ctrl+Shift+S"。返回注册结果。
    // 同一 action 可多次调用以绑定多个键（snow 的多键配置）。
    RegistrationStatus registerHotkey(const QString &action, const QString &keyString);
    // 注销某个 action 下的全部键（换键前先清旧绑定）
    void unregisterAction(const QString &action);
    void unregisterAll();

    // 某个 action 下注册失败的键（被别的程序占用 / 无法解析）。
    // 首选项用它把**单个键**标红，而不是整行标红。
    QStringList failedKeys(const QString &action) const;
    // 有任一键注册失败的 action 集合（托盘菜单/行状态用）。
    QSet<QString> conflictedActions() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    // 全屏时屏蔽快捷键（首选项 → 通用 → 「全屏应用时屏蔽快捷键」）。
    // 开启后，检测到前台是全屏应用时热键**不触发**（键仍然被我们占用，
    // 但不响应），避免和游戏按键冲突。判定逻辑对齐 Snow Shot。
    void setBlockOnFullscreen(bool enabled);
    bool blockOnFullscreen() const { return m_blockOnFullscreen; }

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
    // 一个动作可绑定多个键（snow 的多键配置）：action -> 该动作的全部绑定
    QHash<QString, QVector<Binding>> m_byAction;
    QHash<int, QString>     m_byId;
    // action -> 注册失败的键名（每次 register/unregister 后维护）
    QHash<QString, QStringList> m_failedKeys;
    bool m_blockOnFullscreen = false;
    // 全屏检测结果缓存：WM_HOTKEY 回调里做跨进程查询会卡输入
    mutable bool   m_lastFullscreen = false;
    mutable qint64 m_lastFullscreenCheckMs = 0;
};
