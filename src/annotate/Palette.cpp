#include "annotate/Palette.h"

#include "core/ConfigManager.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

Palette *Palette::instance()
{
    static Palette s_instance;
    return &s_instance;
}

Palette::Palette()
{
    m_colors = defaultColors();
}

QVector<QColor> Palette::defaultColors() const
{
    return {
        QColor(0xFF, 0x4A, 0x4A), QColor(0xFF, 0x9F, 0x1C), QColor(0xFF, 0xD6, 0x0A),
        QColor(0x34, 0xC7, 0x59), QColor(0x3D, 0x8B, 0xFD), QColor(0xAF, 0x52, 0xDE),
        QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0x00, 0x00)
    };
}

QColor Palette::at(int i) const
{
    return (i >= 0 && i < m_colors.size()) ? m_colors.at(i) : QColor();
}

void Palette::setColorAt(int i, const QColor &c)
{
    if (i < 0 || i >= m_colors.size() || !c.isValid())
        return;
    m_colors[i] = c;
    save();
}

void Palette::insertColor(int i, const QColor &c)
{
    if (!c.isValid())
        return;
    m_colors.insert(qBound(0, i, m_colors.size()), c);
    save();
}

void Palette::removeColorAt(int i)
{
    if (i < 0 || i >= m_colors.size() || m_colors.size() <= 1)
        return;
    m_colors.removeAt(i);
    save();
}

void Palette::moveColor(int from, int to)
{
    if (from == to || from < 0 || from >= m_colors.size())
        return;
    to = qBound(0, to, m_colors.size() - 1);
    m_colors.move(from, to);
    save();
}

void Palette::resetToDefault()
{
    m_colors = defaultColors();
    save();
}

void Palette::load()
{
    const QString raw = ConfigManager::instance()
                            ->value(QStringLiteral("interface/palette/colors"))
                            .toString();
    if (raw.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isArray())
        return;

    QVector<QColor> colors;
    for (const QJsonValue &v : doc.array()) {
        const QColor c(v.toString());
        if (c.isValid())
            colors.append(c);
    }
    if (!colors.isEmpty())
        m_colors = colors;
}

void Palette::save() const
{
    QJsonArray arr;
    for (const QColor &c : m_colors)
        arr.append(c.name(QColor::HexArgb));

    ConfigManager::instance()->setValue(QStringLiteral("interface/palette/colors"),
                                        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

bool Palette::exportToFile(const QString &path) const
{
    QJsonArray arr;
    for (const QColor &c : m_colors)
        arr.append(c.name(QColor::HexArgb));

    QJsonObject root;
    root[QStringLiteral("name")] = m_name;
    root[QStringLiteral("colors")] = arr;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool Palette::importFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("colors")).toArray();

    QVector<QColor> colors;
    for (const QJsonValue &v : arr) {
        const QColor c(v.toString());
        if (c.isValid())
            colors.append(c);
    }
    if (colors.isEmpty())
        return false;

    m_colors = colors;
    const QString name = root.value(QStringLiteral("name")).toString();
    if (!name.isEmpty())
        m_name = name;
    save();
    return true;
}
