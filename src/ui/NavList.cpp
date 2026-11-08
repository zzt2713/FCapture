#include "ui/NavList.h"

#include "ui/ElaThemeBridge.h"

#include "ElaTheme.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr int kNavWidth = 168;
} // namespace

NavList::NavList(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(kNavWidth);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TranslucentBackground);
    connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
            this, qOverload<>(&NavList::update));
}

void NavList::addItem(ElaIconType::IconName icon, const QString &title)
{
    m_items.push_back({icon, title});
    setMinimumHeight(m_items.size() * kItemHeight);
    update();
}

int NavList::count() const
{
    return m_items.size();
}

int NavList::currentIndex() const
{
    return m_current;
}

void NavList::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size() || index == m_current)
        return;
    m_current = index;
    update();
    emit currentChanged(index);
}

void NavList::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
    const QColor textColor = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
    const QColor primary = eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal);
    const QColor hoverBg = eTheme->getThemeColor(mode, ElaThemeType::BasicHover);

    for (int i = 0; i < m_items.size(); ++i) {
        const QRect row(0, i * kItemHeight, width(), kItemHeight);
        const bool selected = (i == m_current);

        if (selected) {
            // 选中：淡主色胶囊 + 左侧指示条
            QColor bg = primary;
            bg.setAlpha(38);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(row.adjusted(8, 3, -8, -3), 6, 6);

            p.setBrush(primary);
            p.drawRoundedRect(QRect(0, row.top() + 9, 3, kItemHeight - 18), 2, 2);
        } else if (i == m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(hoverBg);
            p.drawRoundedRect(row.adjusted(8, 3, -8, -3), 6, 6);
        }

        // 图标：ElaAwesome 是图标字体，字形即图标，矢量绘制不糊
        QFont iconFont(QStringLiteral("ElaAwesome"));
        iconFont.setPixelSize(kIconSize);
        p.setFont(iconFont);
        p.setPen(selected ? primary : textColor);
        const QRect iconRect(row.left() + 18, row.top(), kIconSize + 4, kItemHeight);
        p.drawText(iconRect, Qt::AlignCenter | Qt::TextDontClip,
                   QString(QChar(static_cast<ushort>(m_items.at(i).icon))));

        // 文字
        QFont textFont = font();
        textFont.setPixelSize(14);
        p.setFont(textFont);
        p.setPen(selected ? primary : textColor);
        p.drawText(row.adjusted(44, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   m_items.at(i).title);
    }
}

void NavList::mousePressEvent(QMouseEvent *event)
{
    const int row = event->pos().y() / kItemHeight;
    if (row >= 0 && row < m_items.size())
        setCurrentIndex(row);
}

void NavList::mouseMoveEvent(QMouseEvent *event)
{
    const int row = event->pos().y() / kItemHeight;
    const int hover = (row >= 0 && row < m_items.size()) ? row : -1;
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void NavList::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
