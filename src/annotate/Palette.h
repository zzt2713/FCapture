#pragma once

#include <QColor>
#include <QString>
#include <QVector>

// 标注调色板：可拖拽排序、可增删、可导入导出（JSON）
class Palette
{
public:
    static Palette *instance();

    const QVector<QColor> &colors() const { return m_colors; }
    int  count() const { return m_colors.size(); }
    QColor at(int i) const;

    void setColorAt(int i, const QColor &c);
    void insertColor(int i, const QColor &c);
    void removeColorAt(int i);
    void moveColor(int from, int to);
    void resetToDefault();

    void load();
    void save() const;

    // 导出 / 导入：JSON { "name": ..., "colors": [...] }
    bool exportToFile(const QString &path) const;
    bool importFromFile(const QString &path);

private:
    Palette();
    QVector<QColor> defaultColors() const;

    QVector<QColor> m_colors;
    QString m_name = QStringLiteral("My Palette");
};
