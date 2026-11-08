#include "ui/ElaThemeBridge.h"

#include "core/ConfigManager.h"

#include "ElaTheme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace {

const QString kThemeConfigKey = QStringLiteral("interface/theme");

// 将配置值收敛为受支持的三种偏好，未知值回退到暗色。
QString normalizedThemePreference(const QString &preference)
{
    const QString normalized = preference.trimmed().toLower();
    if (normalized == QStringLiteral("system") || normalized == QStringLiteral("light"))
        return normalized;
    return QStringLiteral("dark");
}

// 解析系统配色；Qt 在无法识别时返回 Unknown，此处按项目默认暗色回退。
ElaThemeType::ThemeMode systemThemeMode()
{
    const QStyleHints *hints = QGuiApplication::styleHints();
    if (hints && hints->colorScheme() == Qt::ColorScheme::Light)
        return ElaThemeType::Light;
    return ElaThemeType::Dark;
}

// 根据界面偏好解析实际 Ela 主题模式。
ElaThemeType::ThemeMode resolvedThemeMode(const QString &preference)
{
    if (preference == QStringLiteral("system"))
        return systemThemeMode();
    return preference == QStringLiteral("light") ? ElaThemeType::Light
                                                  : ElaThemeType::Dark;
}

// 让普通 QWidget、滚动视口和 Fusion 控件与 Ela 主题使用同一套基础色。
void applyApplicationPalette(ElaThemeType::ThemeMode mode)
{
    QPalette palette = qApp->palette();
    const QColor windowBase = eTheme->getThemeColor(mode, ElaThemeType::DialogBase);
    const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
    const QColor disabledText =
        eTheme->getThemeColor(mode, ElaThemeType::BasicTextDisable);
    const QColor highlightedText =
        eTheme->getThemeColor(mode, ElaThemeType::BasicTextInvert);

    const QPalette::ColorGroup groups[] = {QPalette::Active, QPalette::Inactive};
    for (QPalette::ColorGroup group : groups) {
        palette.setColor(group, QPalette::Window, windowBase);
        palette.setColor(group, QPalette::WindowText, text);
        palette.setColor(group, QPalette::Base,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicBase));
        palette.setColor(group, QPalette::AlternateBase,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicAlternating));
        palette.setColor(group, QPalette::Text, text);
        palette.setColor(group, QPalette::Button, windowBase);
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::ToolTipBase,
                         eTheme->getThemeColor(mode, ElaThemeType::PopupBase));
        palette.setColor(group, QPalette::ToolTipText, text);
        palette.setColor(group, QPalette::Highlight,
                         eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal));
        palette.setColor(group, QPalette::HighlightedText, highlightedText);
        palette.setColor(group, QPalette::Link,
                         eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal));
        palette.setColor(group, QPalette::PlaceholderText,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicTextNoFocus));
    }

    palette.setColor(QPalette::Disabled, QPalette::Window, windowBase);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Base,
                     eTheme->getThemeColor(mode, ElaThemeType::BasicDisable));
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, windowBase);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);

    qApp->setPalette(palette);
}

// Dark 和 Light 共用项目强调色，避免两套主题行为不一致。
void applyProjectPrimaryColors()
{
    const QColor normal(0x3D, 0x8B, 0xFD);
    const QColor hover(0x5A, 0x9D, 0xFD);
    const QColor press(0x2C, 0x6F, 0xE0);
    const ElaThemeType::ThemeMode modes[] = {ElaThemeType::Dark, ElaThemeType::Light};

    for (ElaThemeType::ThemeMode mode : modes) {
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryNormal, normal);
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryHover, hover);
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryPress, press);
    }
}

} // namespace

ElaThemeBridge *ElaThemeBridge::instance()
{
    static ElaThemeBridge *bridge = nullptr;
    if (!bridge)
        bridge = new ElaThemeBridge(QApplication::instance());
    return bridge;
}

ElaThemeBridge::ElaThemeBridge(QObject *parent)
    : QObject(parent)
{
}

void ElaThemeBridge::initialize()
{
    if (m_initialized)
        return;
    m_initialized = true;

    applyProjectPrimaryColors();
    m_themePreference =
        normalizedThemePreference(ConfigManager::instance()->value(kThemeConfigKey).toString());

    connect(ConfigManager::instance(), &ConfigManager::configChanged,
            this, &ElaThemeBridge::handleConfigChanged);

    if (QStyleHints *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    if (m_themePreference == QStringLiteral("system"))
                        applyCurrentTheme();
                });
    }

    applyCurrentTheme();
}

QString ElaThemeBridge::themePreference() const
{
    return m_themePreference;
}

void ElaThemeBridge::setThemePreference(const QString &preference)
{
    const QString normalized = normalizedThemePreference(preference);
    if (normalized != m_themePreference) {
        m_themePreference = normalized;
        applyCurrentTheme();
    }

    if (ConfigManager::instance()->value(kThemeConfigKey).toString() != normalized)
        ConfigManager::instance()->setValue(kThemeConfigKey, normalized);
}

void ElaThemeBridge::handleConfigChanged(const QString &key)
{
    if (key != kThemeConfigKey)
        return;

    const QString normalized = normalizedThemePreference(
        ConfigManager::instance()->value(kThemeConfigKey).toString());
    if (normalized == m_themePreference)
        return;

    m_themePreference = normalized;
    applyCurrentTheme();
}

void ElaThemeBridge::applyCurrentTheme()
{
    const ElaThemeType::ThemeMode mode = resolvedThemeMode(m_themePreference);
    eTheme->setThemeMode(mode);
    applyApplicationPalette(mode);
    emit themeChanged();
}
