#include "tools/HotCorner.h"

#include "core/ConfigManager.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

HotCorner::HotCorner(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &HotCorner::tick);

    reloadSettings();
    setEnabled(ConfigManager::instance()->value(QStringLiteral("hotcorner/enabled")).toBool());
}

HotCorner::~HotCorner() = default;

QString HotCorner::cornerKey(Corner corner)
{
    switch (corner) {
    case Corner::TopLeft:     return QStringLiteral("hotcorner/top_left");
    case Corner::TopRight:    return QStringLiteral("hotcorner/top_right");
    case Corner::BottomLeft:  return QStringLiteral("hotcorner/bottom_left");
    case Corner::BottomRight: return QStringLiteral("hotcorner/bottom_right");
    }
    return {};
}

QString HotCorner::actionKey(Action action)
{
    switch (action) {
    case Action::HideAllPins:  return QStringLiteral("hide-all");
    case Action::ShowAllPins:  return QStringLiteral("show-all");
    case Action::TogglePins:   return QStringLiteral("toggle-all");
    case Action::Capture:      return QStringLiteral("capture");
    case Action::Paste:        return QStringLiteral("paste");
    case Action::None:         break;
    }
    return QStringLiteral("none");
}

HotCorner::Action HotCorner::actionFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QStringLiteral("hide-all"))   return Action::HideAllPins;
    if (v == QStringLiteral("show-all"))   return Action::ShowAllPins;
    if (v == QStringLiteral("toggle-all")) return Action::TogglePins;
    if (v == QStringLiteral("capture"))    return Action::Capture;
    if (v == QStringLiteral("paste"))      return Action::Paste;
    return Action::None;
}

void HotCorner::reloadSettings()
{
    ConfigManager *cfg = ConfigManager::instance();
    m_actions[0] = actionFromString(cfg->value(cornerKey(Corner::TopLeft)).toString());
    m_actions[1] = actionFromString(cfg->value(cornerKey(Corner::TopRight)).toString());
    m_actions[2] = actionFromString(cfg->value(cornerKey(Corner::BottomLeft)).toString());
    m_actions[3] = actionFromString(cfg->value(cornerKey(Corner::BottomRight)).toString());
}

void HotCorner::setEnabled(bool on)
{
    m_enabled = on;
    if (on) {
        reloadSettings();
        m_timer->start();
    } else {
        m_timer->stop();
        for (bool &in : m_inside)
            in = false;
    }
    ConfigManager::instance()->setValue(QStringLiteral("hotcorner/enabled"), on);
}

void HotCorner::tick()
{
    if (!m_enabled)
        return;

    const QPoint pos = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(pos);
    if (!screen)
        return;

    const QRect geo = screen->geometry();
    const QRect zones[4] = {
        QRect(geo.topLeft(), QSize(kZoneSize, kZoneSize)),
        QRect(QPoint(geo.right() - kZoneSize, geo.top()), QSize(kZoneSize, kZoneSize)),
        QRect(QPoint(geo.left(), geo.bottom() - kZoneSize), QSize(kZoneSize, kZoneSize)),
        QRect(QPoint(geo.right() - kZoneSize, geo.bottom() - kZoneSize),
              QSize(kZoneSize, kZoneSize)),
    };

    for (int i = 0; i < 4; ++i) {
        const bool inside = zones[i].contains(pos);
        if (inside && !m_inside[i]) {
            // 进入沿：触发一次
            if (m_actions[i] != Action::None)
                emit actionTriggered(m_actions[i]);
        }
        m_inside[i] = inside;
    }
}
