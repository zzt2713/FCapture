#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    static ConfigManager *instance();

    QString hotkey(const QString &action) const;
    void setHotkey(const QString &action, const QString &key);

    // 一个动作可绑定多个快捷键（对齐 Snow Shot 的多键配置）。
    // 存储格式：hotkey/<action> 写逗号分隔的列表；旧版的单键值
    // 读出来就是单元素列表，无需迁移。
    QStringList hotkeys(const QString &action) const;
    void setHotkeys(const QString &action, const QStringList &keys);

    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void setValue(const QString &key, const QVariant &v);

    QString quickSaveFolder() const;
    QString autoSaveFolder() const;

    // 用户图片目录（%USERPROFILE%\Pictures）。首选项里的保存目录为空时
    // 用它作为默认值显示，也是「浏览…」对话框的起始目录。
    static QString defaultSaveFolder();
    bool autoSaveEnabled() const;

signals:
    void configChanged(const QString &key);

private:
    explicit ConfigManager(QObject *parent = nullptr);

    QSettings m_settings;
};
