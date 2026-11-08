#pragma once

#include <QPainter>
#include <QString>
#include <QVector>
#include <QWidget>

#include "ElaDef.h"

// 首选项左侧导航列表。
//
// 不用 ElaListView：Model/View 的图标只能走 QIcon 位图，缩放后必糊。
// 这里直接按 Ela 图标字体（ElaAwesome）用 QPainter 矢量绘制，
// 任意 DPI 下都是清晰的。
class NavList : public QWidget
{
    Q_OBJECT

public:
    explicit NavList(QWidget *parent = nullptr);

    void addItem(ElaIconType::IconName icon, const QString &title);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Item
    {
        ElaIconType::IconName icon;
        QString title;
    };

    static constexpr int kItemHeight = 42;
    static constexpr int kIconSize = 17;

    QVector<Item> m_items;
    int m_current = 0;
    int m_hover = -1;
};
