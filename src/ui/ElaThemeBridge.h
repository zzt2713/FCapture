#pragma once

#include <QObject>
#include <QString>

// 应用级主题桥：统一管理 Ela 主题、系统主题跟随和 QApplication 调色板。
// 仅允许在 main.cpp 与 src/ui/ 内包含 Ela 相关实现。
class ElaThemeBridge final : public QObject
{
    Q_OBJECT

public:
    // 返回进程内唯一实例；实例由 QApplication 托管。
    static ElaThemeBridge *instance();

    // 在 eApp->init() 之后调用一次，完成主题初始化并建立系统主题监听。
    void initialize();

    // 返回当前持久化偏好：system / dark / light。
    QString themePreference() const;

    // 切换主题偏好并立即应用；system 会按当前系统配色解析。
    void setThemePreference(const QString &preference);

signals:
    // Ela 主题模式与应用 Palette 同步完成后发出。
    void themeChanged();

private:
    explicit ElaThemeBridge(QObject *parent = nullptr);

    // 配置项被其他入口修改时，保持桥接状态同步。
    void handleConfigChanged(const QString &key);

    // 解析当前偏好并应用 Ela 模式、主色和 QApplication Palette。
    void applyCurrentTheme();

    QString m_themePreference = QStringLiteral("dark");
    bool m_initialized = false;
};
