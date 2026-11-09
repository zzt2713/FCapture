#include "ui/GroupManagerDialog.h"

#include "ui/ElaThemeBridge.h"

#include "paste/PinManager.h"
#include "paste/PinWindow.h"

#include "ElaContentDialog.h"
#include "ElaDef.h"
#include "ElaInputDialog.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaSplitter.h"
#include "ElaText.h"
#include "ElaTheme.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr int kGroupRowHeight = 36;
constexpr int kGroupIconSize = 16;
constexpr int kThumbSize = 96;
constexpr int kThumbCellWidth = kThumbSize + 32;
constexpr int kThumbCellHeight = kThumbSize + 40;
constexpr int kThumbSpacing = 12;

struct ThumbItem
{
    QImage image;
    QString caption;
    QString toolTip;
    PinWindow *window = nullptr;
};

class ThemedPane final : public QWidget
{
public:
    explicit ThemedPane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(true);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, [this]() { update(); });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(),
                   eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::DialogBase));
    }
};

} // namespace

// ---------------------------------------------------------------- 分组列表

// 自绘的分组列表：图标用 ElaAwesome 矢量字体，DPI 下不糊。
class GroupList : public QWidget
{
    Q_OBJECT

public:
    explicit GroupList(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setAutoFillBackground(false);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, qOverload<>(&GroupList::update));
    }

    void setGroups(const QStringList &groups)
    {
        m_groups = groups;
        setFixedHeight(qMax(1, m_groups.size()) * kGroupRowHeight);
        if (m_current >= m_groups.size())
            m_current = qMax(0, m_groups.size() - 1);
        update();
    }

    QStringList groups() const { return m_groups; }
    QString currentGroup() const
    {
        return (m_current >= 0 && m_current < m_groups.size()) ? m_groups.at(m_current)
                                                              : QString();
    }
    int currentIndex() const { return m_current; }

    void setCurrentGroup(const QString &group)
    {
        const int idx = m_groups.indexOf(group);
        if (idx < 0 || idx == m_current)
            return;
        m_current = idx;
        update();
        emit currentChanged(idx);
    }

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), eTheme->getThemeColor(eTheme->getThemeMode(),
                                                 ElaThemeType::DialogBase));
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);

        const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
        const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
        const QColor primary = eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal);
        const QColor hover = eTheme->getThemeColor(mode, ElaThemeType::BasicHover);

        for (int i = 0; i < m_groups.size(); ++i) {
            const QRect row(0, i * kGroupRowHeight, width(), kGroupRowHeight);
            const QRect pill = row.adjusted(2, 2, -2, -2);
            const bool selected = (i == m_current);

            if (selected) {
                QColor bg = primary;
                bg.setAlpha(38);
                p.setPen(Qt::NoPen);
                p.setBrush(bg);
                p.drawRoundedRect(pill, 6, 6);
            } else if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(hover);
                p.drawRoundedRect(pill, 6, 6);
            }

            // 图标：ElaAwesome 字形，矢量绘制
            QFont iconFont(QStringLiteral("ElaAwesome"));
            iconFont.setPixelSize(kGroupIconSize);
            p.setFont(iconFont);
            p.setPen(selected ? primary : text);
            p.drawText(QRect(pill.left() + 8, pill.top(), kGroupIconSize + 4,
                             pill.height()),
                       Qt::AlignCenter | Qt::TextDontClip,
                       QString(QChar(static_cast<ushort>(ElaIconType::Folder))));

            QFont textFont = font();
            textFont.setPixelSize(14);
            p.setFont(textFont);
            p.setPen(selected ? primary : text);
            p.drawText(pill.adjusted(34, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       m_groups.at(i));
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        const int row = e->position().toPoint().y() / kGroupRowHeight;
        if (row < 0 || row >= m_groups.size())
            return;
        if (row == m_current)
            return;
        m_current = row;
        update();
        emit currentChanged(row);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int row = e->position().toPoint().y() / kGroupRowHeight;
        const int hover = (row >= 0 && row < m_groups.size()) ? row : -1;
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        if (m_hover != -1) {
            m_hover = -1;
            update();
        }
    }

private:
    QStringList m_groups;
    int m_current = 0;
    int m_hover = -1;
};

// ---------------------------------------------------------------- 缩略图网格

// 自绘缩略图网格。每个格子显示贴图当前显示内容的等比缩略图 + 尺寸文字。
class ThumbnailGrid : public QWidget
{
    Q_OBJECT

public:
    explicit ThumbnailGrid(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, qOverload<>(&ThumbnailGrid::update));
    }

    void setItems(const QVector<ThumbItem> &items)
    {
        m_items = items;
        relayout();
    }

    void setPlaceholderText(const QString &text)
    {
        m_placeholder = text;
        update();
    }

    void relayout()
    {
        const int available = qMax(1, width());
        m_columns = qMax(1, (available + kThumbSpacing) / (kThumbCellWidth + kThumbSpacing));
        const int rows = (m_items.size() + m_columns - 1) / m_columns;
        setFixedHeight(qMax(1, rows) * (kThumbCellHeight + kThumbSpacing));
        update();
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        relayout();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        // 必须先把整块区域清成对话框底色。
        // 之前是透明背景且只画格子，拖动分隔条触发重绘时旧像素不会被清除，
        // 表现为右侧内容一层层叠加、越拖越乱。
        p.fillRect(rect(), eTheme->getThemeColor(eTheme->getThemeMode(),
                                                 ElaThemeType::DialogBase));
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::TextAntialiasing);

        const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
        const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
        const QColor details = eTheme->getThemeColor(mode, ElaThemeType::BasicDetailsText);
        const QColor border = eTheme->getThemeColor(mode, ElaThemeType::BasicBorder);
        const QColor card = eTheme->getThemeColor(mode, ElaThemeType::BasicBase);

        if (m_items.isEmpty()) {
            QFont f = font();
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(details);
            p.drawText(rect().adjusted(12, 12, -12, -12),
                       Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, m_placeholder);
            return;
        }

        for (int i = 0; i < m_items.size(); ++i) {
            const int col = i % m_columns;
            const int row = i / m_columns;
            const QRect cell(col * (kThumbCellWidth + kThumbSpacing),
                             row * (kThumbCellHeight + kThumbSpacing),
                             kThumbCellWidth, kThumbCellHeight);

            p.setPen(QPen(border, 1));
            p.setBrush(card);
            p.drawRoundedRect(cell.adjusted(0, 0, -1, -1), 6, 6);

            const QImage &img = m_items.at(i).image;
            if (img.isNull())
                continue;

            // 按 dpr 取像素再缩到逻辑尺寸，高 DPI 下不发糊
            const QPixmap scaled = QPixmap::fromImage(
                img.scaled(kThumbSize, kThumbSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));
            const QSize target = scaled.size() / scaled.devicePixelRatio();
            const QRect targetRect(cell.left() + (cell.width() - target.width()) / 2,
                                   cell.top() + 8,
                                   target.width(), target.height());
            p.drawPixmap(targetRect, scaled);

            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(text);
            p.drawText(QRect(cell.left() + 6, cell.bottom() - 22, cell.width() - 12, 18),
                       Qt::AlignCenter, m_items.at(i).caption);
        }
    }

private:
    QVector<ThumbItem> m_items;
    QString m_placeholder;
    int m_columns = 1;
};

// ---------------------------------------------------------------- Dialog

GroupManagerDialog::GroupManagerDialog(QWidget *parent)
    : ElaDialog(parent)
    , m_entryGroup(PinManager::instance()->activeGroup())
{
    setWindowTitle(QStringLiteral("分组管理"));
    setMinimumSize(760, 500);
    resize(820, 540);
    setAppBarHeight(52);   // 关闭按钮可点区跟着放大

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 14);
    layout->setSpacing(0);

    auto *splitter = new ElaSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // ---- 左侧：分组列表 ----
    auto *leftPane = new ThemedPane(splitter);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(14, 10, 8, 0);
    leftLayout->setSpacing(8);

    m_groupList = new GroupList(leftPane);
    leftLayout->addWidget(m_groupList);
    leftLayout->addStretch(1);

    auto *groupButtons = new QHBoxLayout;
    groupButtons->setSpacing(6);
    // 短标签 + 图标：ElaPushButton 的 minimumSizeHint 不含图标宽度，
    // 不显式给最小宽度时图标会被裁掉一半。
    const auto makeButton = [leftPane](const QString &text, ElaIconType::IconName icon) {
        auto *btn = new ElaPushButton(text, leftPane);
        btn->setElaIcon(icon);
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(fm.horizontalAdvance(text) + 64);
        return btn;
    };
    auto *newBtn = makeButton(QStringLiteral("新建"), ElaIconType::Plus);
    auto *renameBtn = makeButton(QStringLiteral("重命名"), ElaIconType::Pen);
    auto *deleteBtn = makeButton(QStringLiteral("删除"), ElaIconType::Trash);
    groupButtons->addWidget(newBtn);
    groupButtons->addWidget(renameBtn);
    groupButtons->addWidget(deleteBtn);
    leftLayout->addLayout(groupButtons);

    // ---- 右侧：缩略图 ----
    auto *rightPane = new ThemedPane(splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(8, 10, 14, 0);
    rightLayout->setSpacing(8);

    m_thumbnailScroll = new QScrollArea(rightPane);
    m_thumbnailScroll->setWidgetResizable(true);
    m_thumbnailScroll->setFrameShape(QFrame::NoFrame);
    m_thumbnailScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thumbnailScroll->setAutoFillBackground(false);
    m_thumbnailScroll->viewport()->setAutoFillBackground(false);

    m_thumbnailGrid = new ThumbnailGrid;
    m_thumbnailGrid->setPlaceholderText(QStringLiteral("当前分组还没有贴图。"));
    m_thumbnailScroll->setWidget(m_thumbnailGrid);
    rightLayout->addWidget(m_thumbnailScroll, 1);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    // ---- 交互 ----
    connect(m_groupList, &GroupList::currentChanged, this, [this](int) {
        const QString group = m_groupList->currentGroup();
        if (!group.isEmpty())
            PinManager::instance()->switchGroup(group);
        reloadThumbnails();
    });

    connect(newBtn, &ElaPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = ElaInputDialog::getText(
            this, QStringLiteral("新建分组"), QStringLiteral("输入分组名称"),
            QStringLiteral("名称"), QString(), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        PinManager::instance()->createGroup(name.trimmed());
        reloadGroups();
    });

    connect(renameBtn, &ElaPushButton::clicked, this, [this]() {
        const QString old = currentGroup();
        if (old.isEmpty())
            return;
        bool ok = false;
        const QString name = ElaInputDialog::getText(
            this, QStringLiteral("重命名分组"), QStringLiteral("输入新的分组名称"),
            QStringLiteral("名称"), old, &ok);
        if (!ok || name.trimmed().isEmpty() || name.trimmed() == old)
            return;
        PinManager::instance()->renameGroup(old, name.trimmed());
        reloadGroups();
    });

    connect(deleteBtn, &ElaPushButton::clicked, this, [this]() {
        const QString group = currentGroup();
        if (group.isEmpty())
            return;
        if (PinManager::instance()->groups().size() <= 1) {
            ElaMessageBar::information(ElaMessageBarType::Top, QStringLiteral("删除分组"),
                                       QStringLiteral("至少保留一个分组"), 2500, this);
            return;
        }

        ElaContentDialog confirm(this);
        confirm.setWindowTitle(QStringLiteral("删除分组"));
        // ElaContentDialog 的中央区没有内边距，文本会贴着边，用带边距的布局包一层
        auto *body = new QWidget(&confirm);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(24, 16, 24, 8);
        auto *msg = new ElaText(QStringLiteral("确定删除分组「%1」？组内贴图会被一起移除。")
                                    .arg(group),
                                body);
        msg->setTextStyle(ElaTextType::Body);
        msg->setWordWrap(true);
        bodyLayout->addWidget(msg);
        confirm.setCentralWidget(body);
        confirm.setLeftButtonText(QStringLiteral("取消"));
        confirm.setMiddleButtonVisible(false);
        confirm.setRightButtonText(QStringLiteral("删除"));
        if (confirm.exec() != QDialog::Accepted)
            return;

        PinManager::instance()->removeGroup(group);
        reloadGroups();
    });

    connect(PinManager::instance(), &PinManager::windowsChanged, this,
            &GroupManagerDialog::reloadThumbnails);
    connect(PinManager::instance(), &PinManager::groupChanged, this,
            &GroupManagerDialog::reloadGroups);
    connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
            this, &GroupManagerDialog::reloadGroups);

    reloadGroups();
    reloadThumbnails();
}

void GroupManagerDialog::closeEvent(QCloseEvent *event)
{
    if (!m_entryGroup.isEmpty()
        && m_entryGroup != PinManager::instance()->activeGroup()) {
        PinManager::instance()->switchGroup(m_entryGroup);
    }
    ElaDialog::closeEvent(event);
}

QString GroupManagerDialog::currentGroup() const
{
    const QString group = m_groupList->currentGroup();
    return group.isEmpty() ? PinManager::instance()->activeGroup() : group;
}

void GroupManagerDialog::reloadGroups()
{
    const QString active = PinManager::instance()->activeGroup();
    m_groupList->setGroups(PinManager::instance()->groups());
    m_groupList->setCurrentGroup(active);
    reloadThumbnails();
}

void GroupManagerDialog::reloadThumbnails()
{
    const QString group = currentGroup();

    QVector<ThumbItem> items;
    for (PinWindow *w : PinManager::instance()->windows()) {
        if (!w || w->group() != group)
            continue;

        const QImage img = w->displayImage();
        if (img.isNull())
            continue;

        ThumbItem item;
        item.image = img;
        item.caption = QStringLiteral("%1 × %2")
                           .arg(w->contentRect().width())
                           .arg(w->contentRect().height());
        item.toolTip = w->sourceText();
        item.window = w;
        items.append(item);
    }

    m_thumbnailGrid->setItems(items);
    m_thumbnailGrid->relayout();
}

#include "GroupManagerDialog.moc"
