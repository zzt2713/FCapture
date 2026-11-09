#include "ui/PreferenceDialog.h"

#include "core/AppPaths.h"
#include "core/ConfigManager.h"
#include "core/Diagnostics.h"
#include "core/Logger.h"
#include "platform/win/WinOcr.h"
#include "platform/win/WinUtil.h"
#include "core/HotkeyManager.h"
#include "core/ImageOutput.h"
#include "core/UpdateChecker.h"
#include "core/UpdateInstaller.h"
#include "paste/PinManager.h"
#include "snip/SnipHistory.h"
#include "tools/HotCorner.h"
#include "ui/ElaThemeBridge.h"
#include "ui/HotkeyRecorder.h"
#include "ui/NavList.h"

#include "ElaComboBox.h"
#include "ElaContentDialog.h"
#include "ElaGroupBox.h"
#include "ElaLineEdit.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaSpinBox.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaToggleSwitch.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QComboBox>
#include <functional>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kToggleWidth = 44;
constexpr int kToggleHeight = 22;

bool sameHotkey(const QString &left, const QString &right)
{
    quint32 leftModifiers = 0;
    quint32 leftKey = 0;
    quint32 rightModifiers = 0;
    quint32 rightKey = 0;
    return HotkeyManager::parseKey(left, &leftModifiers, &leftKey)
           && HotkeyManager::parseKey(right, &rightModifiers, &rightKey)
           && leftModifiers == rightModifiers
           && leftKey == rightKey;
}

QString findHotkeyConflict(const QString &candidate, const QString &currentAction)
{
    static const QStringList kActions = {
        QStringLiteral("capture"),
        QStringLiteral("capture-active-window"),
        QStringLiteral("paste"),
        QStringLiteral("toggle-images"),
        QStringLiteral("long-screenshot"),
    };

    for (const QString &action : kActions) {
        if (action == currentAction)
            continue;
        if (sameHotkey(candidate, ConfigManager::instance()->hotkey(action)))
            return action;
    }
    return QString();
}

QColor dialogBaseColor()
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), ElaThemeType::DialogBase);
}

void applyWindowTheme(QWidget *widget)
{
    const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
    const QColor window = dialogBaseColor();
    const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
    const QColor disabledText =
        eTheme->getThemeColor(mode, ElaThemeType::BasicTextDisable);

    QPalette palette = widget->palette();
    const QPalette::ColorGroup groups[] = {QPalette::Active, QPalette::Inactive};
    for (QPalette::ColorGroup group : groups) {
        palette.setColor(group, QPalette::Window, window);
        palette.setColor(group, QPalette::WindowText, text);
        palette.setColor(group, QPalette::Base,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicBase));
        palette.setColor(group, QPalette::AlternateBase,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicAlternating));
        palette.setColor(group, QPalette::Text, text);
        palette.setColor(group, QPalette::Button, window);
        palette.setColor(group, QPalette::ButtonText, text);
        palette.setColor(group, QPalette::PlaceholderText,
                         eTheme->getThemeColor(mode, ElaThemeType::BasicTextNoFocus));
    }
    palette.setColor(QPalette::Disabled, QPalette::Window, window);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Base,
                     eTheme->getThemeColor(mode, ElaThemeType::BasicDisable));
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Button, window);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);

    widget->setPalette(palette);
    widget->setBackgroundRole(QPalette::Window);
    widget->setAutoFillBackground(true);
    widget->update();
}

// Ela 的输入类控件（ElaSpinBox / ElaLineEdit / ElaComboBox）会给内部的
// QLineEdit 子控件单独设一份调色板，切换主题时这份调色板不会跟着 Ela 主题走，
// 于是浅色主题下输入框里的字仍然是白色（白底白字）。这里显式按当前主题重设。
void applyInputTheme(QWidget *box)
{
    const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
    const QColor base = eTheme->getThemeColor(mode, ElaThemeType::BasicBase);
    const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
    const QColor disabled =
        eTheme->getThemeColor(mode, ElaThemeType::BasicTextDisable);
    const QColor highlight =
        eTheme->getThemeColor(mode, ElaThemeType::PrimaryNormal);
    const QColor highlightedText =
        eTheme->getThemeColor(mode, ElaThemeType::BasicTextInvert);

    const auto applyPalette = [&](QWidget *widget) {
        QPalette palette = widget->palette();
        const QPalette::ColorGroup groups[] = {QPalette::Active, QPalette::Inactive};
        for (QPalette::ColorGroup group : groups) {
            palette.setColor(group, QPalette::Base, base);
            palette.setColor(group, QPalette::Text, text);
            palette.setColor(group, QPalette::Button, base);
            palette.setColor(group, QPalette::ButtonText, text);
            palette.setColor(group, QPalette::Highlight, highlight);
            palette.setColor(group, QPalette::HighlightedText, highlightedText);
            palette.setColor(group, QPalette::PlaceholderText, disabled);
        }
        palette.setColor(QPalette::Disabled, QPalette::Base, base);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
        palette.setColor(QPalette::Disabled, QPalette::Button, base);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
        widget->setPalette(palette);
        widget->update();
    };

    applyPalette(box);
    // QSpinBox / QComboBox 内部都有 QLineEdit 子控件（层级因 Ela 实现而异），
    // 必须递归找出来一并处理，否则浅色主题下输入框文字仍是白色。
    const auto edits = box->findChildren<QLineEdit *>();
    for (QLineEdit *edit : edits)
        applyPalette(edit);
    // 下拉弹层是独立窗口且常在首次展开时才建好样式，颜色最容易滞后
    if (auto *combo = qobject_cast<QComboBox *>(box))
        applyPalette(combo->view());
}

// 一次性处理某个设置页里的所有输入类控件
void themeInputs(QWidget *root)
{
    if (!root)
        return;
    for (QWidget *w : root->findChildren<QWidget *>(QString(), Qt::FindChildrenRecursively)) {
        if (qobject_cast<ElaSpinBox *>(w) || qobject_cast<ElaLineEdit *>(w)
            || qobject_cast<ElaComboBox *>(w)) {
            applyInputTheme(w);
        }
    }
}

// 设置页容器：把内容宽度钉死为视口宽度，只在纵向滚动。
//
// 为什么要专门子类化：Ela 控件的 minimumSizeHint 偏大，
// QScrollArea 即使开了 setWidgetResizable(true) 也只会把内容撑到
// minimumSizeHint 那么宽，超出视口的部分被裁掉（开关、浏览按钮全被推出去）。
class FittedScrollArea : public QScrollArea
{
public:
    explicit FittedScrollArea(QWidget *parent = nullptr) : QScrollArea(parent)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setAutoFillBackground(true);
        viewport()->setAutoFillBackground(true);

        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, &FittedScrollArea::applyTheme);
        applyTheme();
    }

    void setOnShow(std::function<void()> onShow) { m_onShow = std::move(onShow); }

protected:
    void showEvent(QShowEvent *event) override
    {
        QScrollArea::showEvent(event);
        // 页面重新可见时兜底重刷输入控件主题（下拉弹层等首次展开才定型）
        if (m_onShow)
            m_onShow();
        // 首次显示时子控件布局还没结算完，滚动条会按过期的 sizeHint 误判
        // （表现为"刚打开有滚动条，切一次页面就消失"）。
        // 这里做两轮结算：先把宽度夹紧并同步激活布局，再让 QScrollArea
        // 重算滚动条 —— 等价于"切走再切回"的修复效果，但自动完成。
        // 两轮是因为 Ela 控件的尺寸要等 polish/字体结算后才稳定。
        settle(0);
        settle(60);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        // 必须先夹紧宽度再让基类算滚动条：基类按夹紧前的 sizeHint 判断，
        // 偏宽的内容会让虚高的文本换行变多、高度偏大，误显示竖向滚动条。
        fitWidget();
        QScrollArea::resizeEvent(event);
    }

private:
    std::function<void()> m_onShow;

    void fitWidget()
    {
        if (QWidget *w = widget()) {
            w->setMinimumWidth(0);
            w->setMaximumWidth(viewport()->width());
        }
    }

    void settle(int delayMs)
    {
        QTimer::singleShot(delayMs, this, [this]() {
            fitWidget();
            if (QWidget *w = widget()) {
                w->updateGeometry();
                if (w->layout())
                    w->layout()->activate();
            }
            QResizeEvent ev(size(), size());
            QScrollArea::resizeEvent(&ev);
        });
    }

    void applyTheme()
    {
        // 视口自己上 Ela 对话框底色，不能靠透明透出（ElaDialog 不画内容区）。
        applyWindowTheme(this);
        applyWindowTheme(viewport());
        if (QWidget *content = widget())
            applyWindowTheme(content);

        const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
        const QColor handle =
            eTheme->getThemeColor(mode, ElaThemeType::ScrollBarHandle);
        setStyleSheet(QStringLiteral(
                          "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
                          "QScrollBar::handle:vertical { background: %1; border-radius: 5px;"
                          " min-height: 30px; }"
                          "QScrollBar::handle:vertical:hover { background: %2; }"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
                          "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                          " background: transparent; }")
                          .arg(handle.name(), handle.lighter(140).name()));
        update();
    }
};

QWidget *wrapScrollable(QWidget *content, std::function<void()> onShow = {})
{
    auto *scroll = new FittedScrollArea;
    scroll->setWidget(content);
    scroll->setOnShow(std::move(onShow));
    return scroll;
}

class ThemedPage final : public QWidget
{
public:
    explicit ThemedPage(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // ElaDialog 不画内容区，页面必须自己填充当前主题底色。
        setAutoFillBackground(true);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, &ThemedPage::applyTheme);
        applyTheme();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), dialogBaseColor());
    }

private:
    void applyTheme()
    {
        applyWindowTheme(this);
        update();
    }
};

class ThemedStack final : public QStackedWidget
{
public:
    explicit ThemedStack(QWidget *parent = nullptr)
        : QStackedWidget(parent)
    {
        setAutoFillBackground(true);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, &ThemedStack::applyTheme);
        applyTheme();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), dialogBaseColor());
    }

private:
    void applyTheme()
    {
        applyWindowTheme(this);
        update();
    }
};

QWidget *page()
{
    auto *w = new ThemedPage;
    auto *layout = new QVBoxLayout(w);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    return w;
}

// 截图历史缩略图网格：自绘。
//
// 不用 ElaListView：QListView 的 IconMode 在 Ela 的委托下不画模型里的缩略图
// （用户反馈"历史页啥也不显示"），而且 QIcon 是位图，高 DPI 下会糊。
class HistoryGrid final : public QWidget
{
public:
    explicit HistoryGrid(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("单击查看大图，双击复制到剪贴板"));
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                this, qOverload<>(&HistoryGrid::update));
    }

    // 这里不用信号：HistoryGrid 定义在 .cpp 的匿名命名空间里，
    // 带 Q_OBJECT 会要求 AUTOMOC 生成一个不存在的 moc 文件。
    // 回调比信号更直接，也不需要连接生命周期管理。
    // 回调带 index：历史只对最新一条保留原图，需要按索引取（见 SnipHistory::imageFor）
    std::function<void(int index)> onRecordActivated;
    std::function<void(int index)> onSaveRequested;
    std::function<void(int index)> onDeleteRequested;

protected:
    // 单击缩略图 = 把这张历史截图直接贴到屏幕上（贴图形式，和 F3 贴图一致）。
    // 右键 = 删除 / 保存。
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        const int index = indexAt(e->position().toPoint());
        if (index < 0 || index >= m_records.size())
            return;
        if (m_records.at(index).image.isNull())
            return;
        if (onRecordActivated)
            onRecordActivated(index);
    }

    void contextMenuEvent(QContextMenuEvent *e) override
    {
        const int index = indexAt(e->pos());
        if (index < 0 || index >= m_records.size())
            return;
        const bool hasImage = !m_records.at(index).image.isNull();

        QMenu menu(this);
        QAction *save = menu.addAction(QStringLiteral("保存图片…"));
        menu.addSeparator();
        QAction *remove = menu.addAction(QStringLiteral("从历史中删除"));
        save->setEnabled(hasImage);

        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == save && hasImage && onSaveRequested)
            onSaveRequested(index);
        else if (chosen == remove && onDeleteRequested)
            onDeleteRequested(index);
    }

private:
    int indexAt(const QPoint &pos) const
    {
        for (int i = 0; i < m_records.size(); ++i) {
            const int col = i % m_columns;
            const int row = i / m_columns;
            const QRect cell(col * (kCellWidth + kGap), row * (kCellHeight + kGap),
                             kCellWidth, kCellHeight);
            if (cell.contains(pos))
                return i;
        }
        return -1;
    }

    static constexpr int kThumb = 104;
    static constexpr int kCellWidth = kThumb + 32;
    static constexpr int kCellHeight = kThumb + 44;
    static constexpr int kGap = 12;

public:
    void setRecords(const QVector<SnipRecord> &records)
    {
        m_records = records;
        relayout();
    }

    void relayout()
    {
        const int available = qMax(1, width());
        m_columns = qMax(1, (available + kGap) / (kCellWidth + kGap));
        const int rows = (m_records.size() + m_columns - 1) / m_columns;
        setFixedHeight(qMax(1, rows) * (kCellHeight + kGap));
        update();
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        relayout();
    }

private:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::TextAntialiasing);

        const ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
        const QColor text = eTheme->getThemeColor(mode, ElaThemeType::BasicText);
        const QColor details = eTheme->getThemeColor(mode, ElaThemeType::BasicDetailsText);
        const QColor border = eTheme->getThemeColor(mode, ElaThemeType::BasicBorder);
        const QColor card = eTheme->getThemeColor(mode, ElaThemeType::BasicBase);

        for (int i = 0; i < m_records.size(); ++i) {
            const int col = i % m_columns;
            const int row = i / m_columns;
            const QRect cell(col * (kCellWidth + kGap), row * (kCellHeight + kGap),
                             kCellWidth, kCellHeight);

            p.setPen(QPen(border, 1));
            p.setBrush(card);
            p.drawRoundedRect(cell.adjusted(0, 0, -1, -1), 6, 6);

            const QImage &img = m_records.at(i).image;
            if (!img.isNull()) {
                const QPixmap pm = QPixmap::fromImage(
                    img.scaled(kThumb, kThumb, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation));
                const QSize target = pm.size() / pm.devicePixelRatio();
                const QRect targetRect(cell.left() + (cell.width() - target.width()) / 2,
                                       cell.top() + 8, target.width(), target.height());
                p.drawPixmap(targetRect, pm);
            }

            QFont f = font();
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(text);
            const QRect &rec = m_records.at(i).logicalRect;
            p.drawText(QRect(cell.left() + 6, cell.bottom() - 32, cell.width() - 12, 16),
                       Qt::AlignCenter,
                       QStringLiteral("%1 × %2").arg(rec.width()).arg(rec.height()));

            p.setPen(details);
            p.drawText(QRect(cell.left() + 6, cell.bottom() - 17, cell.width() - 12, 14),
                       Qt::AlignCenter,
                       m_records.at(i).timestamp.toString(QStringLiteral("HH:mm:ss")));
        }
    }

    QVector<SnipRecord> m_records;
    int m_columns = 1;
};

// 让 Ela 输入类控件可以被布局压窄。
// 它们的 minimumSizeHint 比可视宽度大，不处理后整行会被推出视口右侧，
// 表现为「输入框伸出窗口，右侧按钮/开关消失」。
template <typename T>
T *narrowable(T *widget)
{
    widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    widget->setMinimumWidth(0);
    return widget;
}

// 带分组框的容器，内部用 QVBoxLayout 追加行
QWidget *groupBox(QWidget *parent, const QString &title, QVBoxLayout **outLayout)
{
    auto *box = new ElaGroupBox(title, parent);
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(10);
    *outLayout = layout;
    return box;
}

// 带 Ela 图标的按钮。
//
// ElaPushButton 的 minimumSizeHint 只按文字算，图标会占掉左侧一段固定宽度，
// 短标签（"新建"/"删除"/"浏览…"）在默认尺寸下会把图标裁掉一半。
// 这里按「图标 + 间距 + 文字」显式给最小宽度，并留出左右内边距。
ElaPushButton *iconButton(const QString &text, QWidget *parent,
                          ElaIconType::IconName icon)
{
    auto *btn = new ElaPushButton(text, parent);
    btn->setElaIcon(icon);
    const QFontMetrics fm(btn->font());
    const int width = fm.horizontalAdvance(text) + 16 /*图标*/ + 12 /*间距*/
                      + 32 /*左右内边距*/;
    btn->setMinimumWidth(width);
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    return btn;
}

} // namespace

PreferenceDialog::~PreferenceDialog()
{
    qInfo() << "PreferenceDialog: destroyed";
}

PreferenceDialog::PreferenceDialog(QWidget *parent)
    : ElaDialog(parent)
{
    setWindowTitle(QStringLiteral("首选项"));
    setMinimumSize(820, 600);
    resize(880, 640);
    // 标题栏高一点，关闭/最小化按钮的可点区随之变大。
    // Ela 的按钮尺寸跟 AppBarHeight 绑定，默认 40px 在 2K 屏上显得很小。
    setAppBarHeight(52);
    setWindowButtonFlag(ElaAppBarType::MaximizeButtonHint, false);

    auto *root = new QHBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧导航 ----
    m_nav = new NavList(this);
    root->addWidget(m_nav);

    // ---- 右侧内容堆叠 ----
    m_stack = new ThemedStack(this);
    root->addWidget(m_stack, 1);

    // ElaDialog 标题栏自带关闭按钮，不需要额外的底部"关闭"
    auto *body = new QVBoxLayout(this);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addLayout(root, 1);

    buildPages();
    m_nav->setCurrentIndex(0);
    m_stack->setCurrentIndex(0);
}

void PreferenceDialog::setHotkeyConflicts(const QSet<QString> &actions)
{
    const QSet<QString> previous = m_conflictedActions;
    m_conflictedActions = actions;

    for (const QString &action : previous) {
        if (actions.contains(action))
            continue;
        if (HotkeyRecorder *recorder = m_hotkeyRecorders.value(action))
            recorder->setHotkeyText(ConfigManager::instance()->hotkey(action));
        if (ElaText *hint = m_hotkeyHints.value(action))
            hint->hide();
    }

    for (const QString &action : actions) {
        if (HotkeyRecorder *recorder = m_hotkeyRecorders.value(action))
            recorder->showConflict(ConfigManager::instance()->hotkey(action));
        if (ElaText *hint = m_hotkeyHints.value(action))
            hint->hide();
    }
}

void PreferenceDialog::showHotkeyResult(const QString &action, bool success,
                                        const QString &message)
{
    HotkeyRecorder *recorder = m_hotkeyRecorders.value(action);
    ElaText *hint = m_hotkeyHints.value(action);
    const QString activeHotkey = ConfigManager::instance()->hotkey(action);

    if (recorder) {
        if (success) {
            m_conflictedActions.remove(action);
            recorder->setHotkeyText(activeHotkey);
        } else {
            m_conflictedActions.insert(action);
            recorder->showConflict(activeHotkey);
            if (hint)
                hint->hide();
            return;
        }
    }

    if (!hint)
        return;
    if (success || message.isEmpty()) {
        hint->hide();
        return;
    }

    hint->setText(message);
    hint->show();
}

void PreferenceDialog::showEvent(QShowEvent *event)
{
    ElaDialog::showEvent(event);

    const QSet<QString> conflictedActions = m_conflictedActions;
    for (const QString &action : conflictedActions) {
        if (HotkeyRecorder *recorder = m_hotkeyRecorders.value(action))
            recorder->showConflict(ConfigManager::instance()->hotkey(action));
    }
}

void PreferenceDialog::hideEvent(QHideEvent *event)
{
    if (m_recordingRecorder)
        m_recordingRecorder->cancelRecording();
    ElaDialog::hideEvent(event);

    // ElaDialog 的关闭按钮走的是 hide() 而不是 close()，
    // 所以 WA_DeleteOnClose 不会生效。这里主动销毁，
    // 否则 10 个设置页 + 全部 Ela 控件会常驻十几 MB 且永不回落。
    qInfo() << "PreferenceDialog: hiding, scheduling deletion";
    deleteLater();
}

void PreferenceDialog::buildPages()
{
    struct Entry {
        const char *title;
        ElaIconType::IconName icon;
        QWidget *(*build)(PreferenceDialog *);
    };

    const Entry entries[] = {
        { "通用", ElaIconType::Gear,        [](PreferenceDialog *d) { return d->buildGeneralPage(); } },
        { "截图", ElaIconType::Crop,        [](PreferenceDialog *d) { return d->buildSnipPage(); } },
        { "控制", ElaIconType::Keyboard,    [](PreferenceDialog *d) { return d->buildControlPage(); } },
        { "贴图", ElaIconType::Image,       [](PreferenceDialog *d) { return d->buildPastePage(); } },
        { "标注", ElaIconType::Pen,         [](PreferenceDialog *d) { return d->buildAnnotatePage(); } },
        { "界面", ElaIconType::Palette,     [](PreferenceDialog *d) { return d->buildInterfacePage(); } },
        { "输出", ElaIconType::FloppyDisk,  [](PreferenceDialog *d) { return d->buildOutputPage(); } },
        { "历史记录", ElaIconType::ClockRotateLeft,
                                            [](PreferenceDialog *d) { return d->buildHistoryPage(); } },
        { "帮助", ElaIconType::CircleQuestion,
                                            [](PreferenceDialog *d) { return d->buildHelpPage(); } },
        { "关于", ElaIconType::CircleInfo,  [](PreferenceDialog *d) { return d->buildAboutPage(); } },
    };

    m_historyPageIndex = 7;
    for (const Entry &e : entries) {
        m_nav->addItem(e.icon, QString::fromUtf8(e.title));
        QWidget *content = e.build(this);
        // 输入类控件要按当前主题重设调色板，并在主题切换时再刷一遍。
        // Ela 内部会异步重设输入控件的内部调色板，切换主题时要晚一步
        // 再覆盖一次，否则输入框文字会残留旧主题的颜色（白底白字/黑底黑字）。
        themeInputs(content);
        connect(ElaThemeBridge::instance(), &ElaThemeBridge::themeChanged,
                content, [content]() {
                    themeInputs(content);
                    QTimer::singleShot(0, content, [content]() { themeInputs(content); });
                    QTimer::singleShot(150, content, [content]() { themeInputs(content); });
                });
        // 切到该页时兜底再刷一遍（覆盖下拉弹层等首次展开才定型的控件）
        m_stack->addWidget(wrapScrollable(content, [content]() { themeInputs(content); }));
    }

    // 一次只显示一页，避免隐藏页仍参与绘制
    connect(m_nav, &NavList::currentChanged, this, [this](int row) {
        for (int i = 0; i < m_stack->count(); ++i)
            m_stack->widget(i)->setVisible(i == row);
        m_stack->setCurrentIndex(row);
        if (m_historyList && row == m_historyPageIndex)
            refreshHistory();
    });
}

// ---------------------------------------------------------------- 绑定 helper

QWidget *PreferenceDialog::toggleRow(QWidget *parent, const QString &text, const QString &key)
{
    auto *row = new QWidget(parent);
    row->setAutoFillBackground(false);
    row->setAttribute(Qt::WA_TranslucentBackground);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(12);
    rowLayout->addWidget(new ElaText(text, row), 1);

    auto *sw = new ElaToggleSwitch(row);
    // ElaToggleSwitch 没有固定尺寸，布局里会被压扁
    sw->setFixedSize(kToggleWidth, kToggleHeight);
    bindToggle(sw, key);
    rowLayout->addWidget(sw, 0, Qt::AlignVCenter);
    return row;
}

void PreferenceDialog::bindToggle(ElaToggleSwitch *sw, const QString &key)
{
    sw->setIsToggled(ConfigManager::instance()->value(key).toBool());
    connect(sw, &ElaToggleSwitch::toggled, this, [key](bool on) {
        ConfigManager::instance()->setValue(key, on);
    });
}

void PreferenceDialog::bindString(ElaLineEdit *edit, const QString &key)
{
    edit->setText(ConfigManager::instance()->value(key).toString());
    connect(edit, &QLineEdit::editingFinished, this, [edit, key]() {
        ConfigManager::instance()->setValue(key, edit->text());
    });
}

void PreferenceDialog::bindInt(ElaSpinBox *box, const QString &key)
{
    box->setValue(ConfigManager::instance()->value(key).toInt());
    connect(box, &QSpinBox::valueChanged, this, [key](int v) {
        ConfigManager::instance()->setValue(key, v);
    });
}

// ---------------------------------------------------------------- 通用

QWidget *PreferenceDialog::buildGeneralPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *saveLayout = nullptr;
    QWidget *saveBox = groupBox(w, QStringLiteral("保存"), &saveLayout);
    saveLayout->addWidget(toggleRow(saveBox, QStringLiteral("自动保存截图"),
                                    QStringLiteral("general/auto_save_enabled")));

    auto *autoSaveRow = new QWidget(saveBox);
    auto *autoSaveLayout = new QHBoxLayout(autoSaveRow);
    autoSaveLayout->setContentsMargins(0, 0, 0, 0);
    autoSaveLayout->setSpacing(8);
    auto *autoSaveLabel = new ElaText(QStringLiteral("自动保存目录"), autoSaveRow);
    autoSaveLayout->addWidget(autoSaveLabel, 0);
    auto *autoSaveEdit = narrowable(new ElaLineEdit(autoSaveRow));
    // 配置为空时显示默认图片目录，而不是空框：空框既看不出会存到哪，
    // 也容易让人以为自动保存没生效。
    autoSaveEdit->setText(ConfigManager::instance()->autoSaveFolder());
    autoSaveEdit->setPlaceholderText(ConfigManager::defaultSaveFolder());
    connect(autoSaveEdit, &QLineEdit::editingFinished, this, [autoSaveEdit]() {
        ConfigManager::instance()->setValue(QStringLiteral("general/auto_save_folder"),
                                            autoSaveEdit->text().trimmed());
    });
    auto *autoSaveBrowse = new ElaPushButton(QStringLiteral("浏览…"), autoSaveRow);
    connect(autoSaveBrowse, &ElaPushButton::clicked, this, [this, autoSaveEdit]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("自动保存目录"), autoSaveEdit->text().trimmed());
        if (!dir.isEmpty()) {
            autoSaveEdit->setText(dir);
            ConfigManager::instance()->setValue(QStringLiteral("general/auto_save_folder"), dir);
        }
    });
    autoSaveLabel->setFixedWidth(84);
    autoSaveLayout->addWidget(autoSaveEdit, 1);
    autoSaveLayout->addWidget(autoSaveBrowse);
    saveLayout->addWidget(autoSaveRow);

    auto *quickRow = new QWidget(saveBox);
    auto *quickLayout = new QHBoxLayout(quickRow);
    quickLayout->setContentsMargins(0, 0, 0, 0);
    quickLayout->setSpacing(8);
    auto *quickLabel = new ElaText(QStringLiteral("快速保存目录"), quickRow);
    quickLayout->addWidget(quickLabel, 0);
    auto *quickEdit = narrowable(new ElaLineEdit(quickRow));
    // 空配置时显示实际的快速保存目录（图片目录/Snipaste）
    quickEdit->setText(ConfigManager::instance()->quickSaveFolder());
    quickEdit->setPlaceholderText(ConfigManager::defaultSaveFolder());
    connect(quickEdit, &QLineEdit::editingFinished, this, [quickEdit]() {
        ConfigManager::instance()->setValue(QStringLiteral("general/quick_save_folder"),
                                            quickEdit->text().trimmed());
    });
    auto *quickBrowse = new ElaPushButton(QStringLiteral("浏览…"), quickRow);
    connect(quickBrowse, &ElaPushButton::clicked, this, [this, quickEdit]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("快速保存目录"), quickEdit->text().trimmed());
        if (!dir.isEmpty()) {
            quickEdit->setText(dir);
            ConfigManager::instance()->setValue(QStringLiteral("general/quick_save_folder"), dir);
        }
    });
    quickLabel->setFixedWidth(84);
    quickLayout->addWidget(quickEdit, 1);
    quickLayout->addWidget(quickBrowse);
    saveLayout->addWidget(quickRow);

    layout->addWidget(saveBox);

    QVBoxLayout *miscLayout = nullptr;
    QWidget *miscBox = groupBox(w, QStringLiteral("行为"), &miscLayout);
    miscLayout->addWidget(toggleRow(miscBox, QStringLiteral("截图完成时播放提示音"),
                                    QStringLiteral("general/play_sound")));
    miscLayout->addWidget(toggleRow(miscBox, QStringLiteral("记录所有截图（含 Esc 中止的）"),
                                    QStringLiteral("general/record_all_snips")));
    miscLayout->addWidget(toggleRow(miscBox, QStringLiteral("复制图像时同时复制为文件"),
                                    QStringLiteral("general/copy_image_as_file")));
    miscLayout->addWidget(toggleRow(miscBox, QStringLiteral("允许从网页拖拽/粘贴图片"),
                                    QStringLiteral("general/enable_web_image_paste")));
    miscLayout->addWidget(toggleRow(miscBox, QStringLiteral("复制或保存图片后显示提示"),
                                    QStringLiteral("general/show_operation_toast")));

    // 开机自启：直接写 HKCU Run 键，不经 config.ini（注册表才是唯一事实来源）。
    // 便携模式下不提供：便携版的语义就是"不往系统里留东西"。
    auto *autoStartRow = new QWidget(miscBox);
    auto *autoStartLayout = new QHBoxLayout(autoStartRow);
    autoStartLayout->setContentsMargins(0, 0, 0, 0);
    autoStartLayout->setSpacing(12);
    autoStartLayout->addWidget(
        new ElaText(AppPaths::isPortable() ? QStringLiteral("开机自动启动（便携模式不可用）")
                                           : QStringLiteral("开机自动启动"),
                    autoStartRow),
        1);

    auto *autoStart = new ElaToggleSwitch(autoStartRow);
    autoStart->setFixedSize(kToggleWidth, kToggleHeight);
    autoStartLayout->addWidget(autoStart, 0, Qt::AlignVCenter);

    if (AppPaths::isPortable()) {
        autoStart->setEnabled(false);
        autoStart->setIsToggled(false);
    } else {
        autoStart->setIsToggled(WinUtil::isAutoStartEnabled());
        connect(autoStart, &ElaToggleSwitch::toggled, this, [this, autoStart](bool on) {
            if (WinUtil::setAutoStart(on))
                return;
            // 失败要回滚开关状态并告知原因，不能让 UI 与注册表不一致
            const QString err = WinUtil::lastError();
            autoStart->blockSignals(true);
            autoStart->setIsToggled(!on);
            autoStart->blockSignals(false);
            ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("开机自启"),
                                   err.isEmpty() ? QStringLiteral("设置失败") : err, 3000, this);
        });
    }
    miscLayout->addWidget(autoStartRow);
    layout->addWidget(miscBox);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 截图

QWidget *PreferenceDialog::buildSnipPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *detectLayout = nullptr;
    QWidget *detectBox = groupBox(w, QStringLiteral("检测"), &detectLayout);
    detectLayout->addWidget(toggleRow(detectBox, QStringLiteral("自动检测窗口"),
                                      QStringLiteral("snip/auto_detect_window")));
    detectLayout->addWidget(toggleRow(detectBox, QStringLiteral("显示辅助线"),
                                      QStringLiteral("snip/show_auxiliary_lines")));
    detectLayout->addWidget(toggleRow(detectBox, QStringLiteral("截图中包含鼠标光标"),
                                      QStringLiteral("snip/capture_cursor")));
    layout->addWidget(detectBox);

    QVBoxLayout *excludeLayout = nullptr;
    QWidget *excludeBox = groupBox(w, QStringLiteral("排除与降级"), &excludeLayout);
    auto *excludeForm = new QFormLayout;
    excludeForm->setSpacing(8);

    auto *excludeEdit = narrowable(new ElaLineEdit(excludeBox));
    excludeEdit->setPlaceholderText(QStringLiteral("逗号分隔的进程名，如 game.exe"));
    bindString(excludeEdit, QStringLiteral("snip/exclude_from_detection"));
    excludeForm->addRow(new ElaText(QStringLiteral("不参与检测的程序")), excludeEdit);

    auto *msaaEdit = narrowable(new ElaLineEdit(excludeBox));
    msaaEdit->setPlaceholderText(QStringLiteral("逗号分隔，留空用默认列表"));
    bindString(msaaEdit, QStringLiteral("snip/element_msaa_apps"));
    excludeForm->addRow(new ElaText(QStringLiteral("元素检测走 MSAA 的程序")), msaaEdit);
    excludeLayout->addLayout(excludeForm);
    layout->addWidget(excludeBox);

    QVBoxLayout *effectLayout = nullptr;
    QWidget *effectBox = groupBox(w, QStringLiteral("效果"), &effectLayout);
    auto *effectForm = new QFormLayout;
    effectForm->setSpacing(8);

    auto *cornerSpin = narrowable(new ElaSpinBox(effectBox));
    cornerSpin->setRange(0, 50);
    cornerSpin->setSuffix(QStringLiteral(" px"));
    bindInt(cornerSpin, QStringLiteral("snip/corner_radius"));
    effectForm->addRow(new ElaText(QStringLiteral("圆角半径（0 = 关闭）")), cornerSpin);
    effectLayout->addLayout(effectForm);
    effectLayout->addWidget(toggleRow(effectBox, QStringLiteral("导出时加阴影"),
                                      QStringLiteral("snip/shadow_enabled")));
    layout->addWidget(effectBox);

    QVBoxLayout *ocrLayout = nullptr;
    QWidget *ocrBox = groupBox(w, QStringLiteral("OCR"), &ocrLayout);
    auto *ocrForm = new QFormLayout;
    ocrForm->setSpacing(8);
    auto *ocrLang = narrowable(new ElaComboBox(ocrBox));
    // 只读：语言必须是系统真实安装的包，手输一个不存在的 code 只会静默回退
    ocrLang->setEditable(false);
    const QStringList langs = WinOcr::availableLanguages();
    for (const QString &tag : langs)
        ocrLang->addItem(WinOcr::languageDisplayName(tag), tag);

    const QString currentLang =
        ConfigManager::instance()->value(QStringLiteral("ocr/language")).toString();
    if (!currentLang.isEmpty()) {
        const int idx = ocrLang->findData(currentLang);
        if (idx >= 0)
            ocrLang->setCurrentIndex(idx);
    }
    connect(ocrLang, &QComboBox::currentIndexChanged, this, [ocrLang](int) {
        ConfigManager::instance()->setValue(QStringLiteral("ocr/language"),
                                            ocrLang->currentData().toString());
    });

    ocrForm->addRow(new ElaText(QStringLiteral("识别语言")), ocrLang);
    ocrLayout->addLayout(ocrForm);

    ocrLayout->addWidget(toggleRow(
        ocrBox, QStringLiteral("识别增强（对比度预处理 + 模糊时二次识别，稍慢但更准）"),
        QStringLiteral("ocr/enhance")));

    auto *hint = new ElaText(
        QStringLiteral("可用的 OCR 语言包：%1")
            .arg(langs.isEmpty() ? QStringLiteral("无")
                                 : langs.join(QStringLiteral("、"))),
        ocrBox);
    hint->setTextStyle(ElaTextType::Caption);
    hint->setWordWrap(true);
    ocrLayout->addWidget(hint);

    layout->addWidget(ocrBox);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 控制

QWidget *PreferenceDialog::buildControlPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *hotkeyLayout = nullptr;
    QWidget *hotkeyBox = groupBox(w, QStringLiteral("全局快捷键（修改后立即生效）"), &hotkeyLayout);
    auto *hotkeyForm = new QFormLayout;
    hotkeyForm->setSpacing(8);

    struct Binding { const char *key; const char *label; };
    const Binding bindings[] = {
        { "capture", "截图" },
        { "capture-active-window", "抓活动窗口" },
        { "paste", "贴图" },
        { "toggle-images", "隐藏/显示所有贴图" },
        { "long-screenshot", "长截图" },
    };

    for (const Binding &b : bindings) {
        const QString action = QString::fromLatin1(b.key);

        // 自绘录制框直接处理 QKeyEvent，不依赖 ElaKeyBinder 的私有事件循环。
        auto *recorder = new HotkeyRecorder(hotkeyBox);
        // 必须走 hotkey()：它会在配置缺项时回落到默认值。
        // 直接 value("hotkey/xxx") 拿到空串再回写，会把默认热键永久抹成空。
        recorder->setHotkeyText(ConfigManager::instance()->hotkey(action));

        auto *hint = new ElaText(hotkeyBox);
        hint->setTextStyle(ElaTextType::Caption);
        hint->hide();
        m_hotkeyRecorders.insert(action, recorder);
        m_hotkeyHints.insert(action, hint);

        connect(recorder, &HotkeyRecorder::validationFailed, this,
                [hint](const QString &message) {
                    hint->setText(message);
                    hint->show();
                });

        connect(recorder, &HotkeyRecorder::recordingStarted, this,
                [this, recorder]() {
                    if (m_recordingRecorder == recorder)
                        return;
                    // 同一时间只允许一个录制框持有焦点；切换前先结束旧录制。
                    if (m_recordingRecorder)
                        m_recordingRecorder->cancelRecording();
                    m_recordingRecorder = recorder;
                    emit hotkeyRecordingChanged(true);
                });

        connect(recorder, &HotkeyRecorder::recordingFinished, this,
                [this, recorder]() {
                    if (m_recordingRecorder != recorder)
                        return;
                    m_recordingRecorder.clear();
                    emit hotkeyRecordingChanged(false);
                });

        connect(recorder, &HotkeyRecorder::hotkeyRecorded, this,
                [this, action](const QString &hotkey) {
                    const QString conflict = findHotkeyConflict(hotkey, action);
                    if (!conflict.isEmpty()) {
                        showHotkeyResult(action, false, QStringLiteral("快捷键被占用"));
                        return;
                    }

                    emit hotkeyChangeRequested(action, hotkey);
                });

        auto *cell = new QWidget(hotkeyBox);
        cell->setAutoFillBackground(false);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);
        cellLayout->addWidget(recorder);
        cellLayout->addWidget(hint);

        hotkeyForm->addRow(new ElaText(QString::fromUtf8(b.label)), cell);
    }
    hotkeyLayout->addLayout(hotkeyForm);
    layout->addWidget(hotkeyBox);

    QVBoxLayout *cornerLayout = nullptr;
    QWidget *cornerBox = groupBox(w, QStringLiteral("屏幕热角"), &cornerLayout);
    cornerLayout->addWidget(toggleRow(cornerBox, QStringLiteral("启用热角"),
                                      QStringLiteral("hotcorner/enabled")));

    auto *cornerForm = new QFormLayout;
    cornerForm->setSpacing(8);
    const char *cornerLabels[] = { "左上", "右上", "左下", "右下" };
    const HotCorner::Corner corners[] = {
        HotCorner::Corner::TopLeft, HotCorner::Corner::TopRight,
        HotCorner::Corner::BottomLeft, HotCorner::Corner::BottomRight
    };
    const char *actionValues[] = { "none", "hide-all", "show-all", "toggle-all", "capture", "paste" };
    const char *actionLabels[] = { "无", "隐藏所有贴图", "显示所有贴图", "切换贴图显示",
                                   "截图", "贴图" };

    for (int i = 0; i < 4; ++i) {
        auto *box = narrowable(new ElaComboBox(cornerBox));
        for (int a = 0; a < 6; ++a)
            box->addItem(QString::fromUtf8(actionLabels[a]), QString::fromLatin1(actionValues[a]));

        const QString key = HotCorner::cornerKey(corners[i]);
        const QString current = ConfigManager::instance()->value(key).toString();
        const int idx = box->findData(current);
        if (idx >= 0)
            box->setCurrentIndex(idx);

        connect(box, &QComboBox::currentIndexChanged, this, [box, key](int) {
            ConfigManager::instance()->setValue(key, box->currentData().toString());
        });
        cornerForm->addRow(new ElaText(QString::fromUtf8(cornerLabels[i])), box);
    }
    cornerLayout->addLayout(cornerForm);
    layout->addWidget(cornerBox);

    QVBoxLayout *superLayout = nullptr;
    QWidget *superBox = groupBox(w, QStringLiteral("Super-snip"), &superLayout);
    superLayout->addWidget(toggleRow(
        superBox,
        QStringLiteral("按住 Win 键拖拽鼠标直接截图（需要安装全局鼠标钩子，部分杀软会提示）"),
        QStringLiteral("super_snip/enabled")));
    layout->addWidget(superBox);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 贴图

QWidget *PreferenceDialog::buildPastePage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *formLayout = nullptr;
    QWidget *box = groupBox(w, QStringLiteral("贴图行为"), &formLayout);
    auto *form = new QFormLayout;
    form->setSpacing(8);

    auto *recoverSpin = narrowable(new ElaSpinBox(box));
    recoverSpin->setRange(0, 20);
    bindInt(recoverSpin, QStringLiteral("general/recover_closed_count"));
    form->addRow(new ElaText(QStringLiteral("可恢复关闭的贴图数量")), recoverSpin);

    auto *magnetSpin = narrowable(new ElaSpinBox(box));
    magnetSpin->setRange(0, 200);
    magnetSpin->setSuffix(QStringLiteral(" px"));
    bindInt(magnetSpin, QStringLiteral("paste/magnetic_attach_threshold"));
    form->addRow(new ElaText(QStringLiteral("吸附距离阈值")), magnetSpin);
    formLayout->addLayout(form);

    formLayout->addWidget(toggleRow(box, QStringLiteral("贴图只在创建它的虚拟桌面上显示"),
                                    QStringLiteral("snip/paste_current_desktop_only")));
    formLayout->addWidget(toggleRow(box, QStringLiteral("粘贴文件路径时优先显示为图片"),
                                    QStringLiteral("general/never_paste_file_path")));
    layout->addWidget(box);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 标注

QWidget *PreferenceDialog::buildAnnotatePage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *boxLayout = nullptr;
    QWidget *box = groupBox(w, QStringLiteral("标注"), &boxLayout);
    boxLayout->addWidget(toggleRow(box, QStringLiteral("创建标注后自动选中"),
                                   QStringLiteral("annotate/auto_select_on_creation")));

    auto *tip = new ElaText(
        QStringLiteral("笔宽档位、默认颜色、调色板管理在截图工具栏里右键对应按钮即可调整，"
                       "设置会自动保存到配置文件。"),
        box);
    tip->setTextStyle(ElaTextType::Caption);
    tip->setWordWrap(true);
    boxLayout->addWidget(tip);
    layout->addWidget(box);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 界面

QWidget *PreferenceDialog::buildInterfacePage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *appearanceLayout = nullptr;
    QWidget *appearanceBox = groupBox(w, QStringLiteral("外观"), &appearanceLayout);
    auto *form = new QFormLayout;
    form->setSpacing(8);

    // 所有主题状态统一由 ElaThemeBridge 管理，页面不在本地直接操作 eTheme。
    auto *theme = narrowable(new ElaComboBox(appearanceBox));
    theme->addItem(QStringLiteral("跟随系统"), QStringLiteral("system"));
    theme->addItem(QStringLiteral("暗色"), QStringLiteral("dark"));
    theme->addItem(QStringLiteral("浅色"), QStringLiteral("light"));
    const QString currentTheme = ElaThemeBridge::instance()->themePreference();
    const int themeIdx = theme->findData(currentTheme);
    if (themeIdx >= 0)
        theme->setCurrentIndex(themeIdx);
    connect(theme, &QComboBox::currentIndexChanged, this, [theme](int) {
        ElaThemeBridge::instance()->setThemePreference(theme->currentData().toString());
    });
    form->addRow(new ElaText(QStringLiteral("主题")), theme);

    auto *unit = narrowable(new ElaComboBox(appearanceBox));
    unit->addItem(QStringLiteral("像素 (px)"), false);
    unit->addItem(QStringLiteral("逻辑像素 (dip)"), true);
    unit->setCurrentIndex(ConfigManager::instance()
                              ->value(QStringLiteral("interface/size_unit_dip")).toBool() ? 1 : 0);
    connect(unit, &QComboBox::currentIndexChanged, this, [unit](int) {
        ConfigManager::instance()->setValue(QStringLiteral("interface/size_unit_dip"),
                                            unit->currentData().toBool());
    });
    form->addRow(new ElaText(QStringLiteral("尺寸单位")), unit);
    appearanceLayout->addLayout(form);

    appearanceLayout->addWidget(toggleRow(appearanceBox, QStringLiteral("取色时使用大写 HEX"),
                                          QStringLiteral("interface/uppercase_hex")));
    layout->addWidget(appearanceBox);

    QVBoxLayout *trayLayout = nullptr;
    QWidget *trayBox = groupBox(w, QStringLiteral("托盘"), &trayLayout);
    trayLayout->addWidget(toggleRow(trayBox, QStringLiteral("隐藏托盘图标"),
                                    QStringLiteral("general/hide_tray_icon")));
    layout->addWidget(trayBox);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 输出

QWidget *PreferenceDialog::buildOutputPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *formLayout = nullptr;
    QWidget *box = groupBox(w, QStringLiteral("文件格式"), &formLayout);
    auto *form = new QFormLayout;
    form->setSpacing(8);

    auto *nameFormat = narrowable(new ElaLineEdit(box));
    nameFormat->setPlaceholderText(QStringLiteral("Snipaste_{yyyy}-{MM}-{dd}_{HH}-{mm}-{ss}"));
    bindString(nameFormat, QStringLiteral("output/file_name_format"));
    form->addRow(new ElaText(QStringLiteral("文件名格式")), nameFormat);

    auto *format = narrowable(new ElaComboBox(box));
    format->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    format->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    format->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    const QString currentFormat =
        ConfigManager::instance()->value(QStringLiteral("output/format")).toString();
    const int idx = format->findData(currentFormat);
    if (idx >= 0)
        format->setCurrentIndex(idx);
    connect(format, &QComboBox::currentIndexChanged, this, [format](int) {
        ConfigManager::instance()->setValue(QStringLiteral("output/format"),
                                            format->currentData().toString());
    });
    form->addRow(new ElaText(QStringLiteral("默认格式")), format);

    auto *quality = narrowable(new ElaSpinBox(box));
    quality->setRange(-1, 100);
    quality->setSpecialValueText(QStringLiteral("自动"));
    quality->setToolTip(QStringLiteral(
        "自动时由 Qt/图像插件决定 JPEG/WebP 质量；PNG 始终无损。"
        "手动值范围为 0-100。"));
    bindInt(quality, QStringLiteral("output/quality"));
    form->addRow(
        new ElaText(QStringLiteral("压缩质量（-1=自动；PNG 始终无损）")),
        quality);
    formLayout->addLayout(form);

    formLayout->addWidget(toggleRow(box, QStringLiteral("图像不含透明像素时自动移除 alpha 通道"),
                                    QStringLiteral("output/auto_alpha_channel_removal")));
    layout->addWidget(box);

    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 历史记录

QWidget *PreferenceDialog::buildHistoryPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *boxLayout = nullptr;
    QWidget *box = groupBox(w, QStringLiteral("截图历史"), &boxLayout);
    // 这一页的内容区要撑满，底部提示/按钮才能贴到页底
    box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_historyList = new HistoryGrid(box);
    boxLayout->addWidget(m_historyList, 1);

    // 「还没有截图记录」和「清空历史」都放到最下方，
    // 让缩略图区域始终从顶部开始排列，空状态也不会顶在标题下面。
    boxLayout->addStretch(1);

    auto *hint = new ElaText(box);
    hint->setTextStyle(ElaTextType::Caption);
    hint->setWordWrap(true);
    m_historyHint = hint;
    boxLayout->addWidget(hint);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *clearBtn = iconButton(QStringLiteral("清空历史"), box, ElaIconType::Trash);
    buttons->addWidget(clearBtn);
    buttons->addStretch(1);
    boxLayout->addLayout(buttons);

    connect(clearBtn, &ElaPushButton::clicked, this, [this]() {
        SnipHistory::instance()->clear();
    });
    connect(SnipHistory::instance(), &SnipHistory::changed,
            this, &PreferenceDialog::refreshHistory);
    auto *grid = static_cast<HistoryGrid *>(m_historyList);
    // 单击 = 以贴图形式贴到屏幕上（和 F3 贴图同一条路径，可缩放/旋转/置顶）
    grid->onRecordActivated = [](int index) {
        // 只有最新一条保留了原图，更早的退化为缩略图（贴出来是缩小版）
        PinManager::instance()->pasteImage(
            SnipHistory::instance()->imageFor(index));
    };
    grid->onSaveRequested = [this](int index) {
        const QImage image = SnipHistory::instance()->imageFor(index);
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存历史截图"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                .filePath(QStringLiteral("Snipaste_%1.png")
                              .arg(QDateTime::currentDateTime().toString(
                                  QStringLiteral("yyyy-MM-dd_HH-mm-ss")))),
            QStringLiteral("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
        if (path.isEmpty())
            return;
        if (ImageOutput::save(image, path))
            ElaMessageBar::success(ElaMessageBarType::Top, QStringLiteral("已保存"),
                                   QDir::toNativeSeparators(path), 2500, this);
        else
            ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("保存失败"),
                                   QDir::toNativeSeparators(path), 2500, this);
    };
    grid->onDeleteRequested = [](int index) {
        SnipHistory::instance()->removeAt(index);
    };

    // box 撑满整页高度，底部提示/按钮才会贴到页底
    layout->addWidget(box, 1);
    return w;
}

void PreferenceDialog::refreshHistory()
{
    if (!m_historyList || !m_historyHint)
        return;

    SnipHistory *history = SnipHistory::instance();
    auto *grid = static_cast<HistoryGrid *>(m_historyList);
    grid->setRecords(history->records());

    if (history->count() == 0) {
        m_historyHint->setText(QStringLiteral(
            "还没有截图记录。每完成一次截图都会进历史（内存保存，退出即清空）。"));
    } else {
        m_historyHint->setText(QStringLiteral(
            "共 %1 条，最近的在最前。单击缩略图贴到屏幕，右键可保存或删除。")
                                   .arg(history->count()));
    }
}

// ---------------------------------------------------------------- 帮助

QWidget *PreferenceDialog::buildHelpPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *hotkeyLayout = nullptr;
    QWidget *hotkeyBox = groupBox(w, QStringLiteral("常用快捷键"), &hotkeyLayout);

    struct Row { const char *key; const char *desc; };
    static const Row kRows[] = {
        { "F1",             "开始截图" },
        { "F3",             "把剪贴板内容贴成图片窗口" },
        { "Shift+F3",       "隐藏 / 显示所有贴图" },
        { "Ctrl+Shift+A",   "截取当前活动窗口" },
        { "Tab",            "切换 自由 / 窗口 / 元素 检测" },
        { "滚轮",           "元素检测时切换父 / 子元素块" },
        { "` / ~",          "截图时显示或隐藏鼠标指针" },
        { "C",              "取色（Shift 切换 HEX / RGB）" },
        { "Enter",          "复制并结束截图" },
        { "Esc",            "中止截图" },
        { ", / .",          "历史截图回放" },
        { "F5",             "刷新截图内容（保留标注）" },
    };

    auto *form = new QFormLayout;
    form->setSpacing(6);
    for (const Row &r : kRows) {
        // key 里可能含中文（如「滚轮」），必须按 UTF-8 解码
        auto *key = new ElaText(QString::fromUtf8(r.key), hotkeyBox);
        key->setTextStyle(ElaTextType::Body);
        auto *desc = new ElaText(QString::fromUtf8(r.desc), hotkeyBox);
        desc->setTextStyle(ElaTextType::Caption);
        form->addRow(key, desc);
    }
    hotkeyLayout->addLayout(form);
    layout->addWidget(hotkeyBox);

    QVBoxLayout *docLayout = nullptr;
    QWidget *docBox = groupBox(w, QStringLiteral("文档与反馈"), &docLayout);
    auto *tip = new ElaText(
        QStringLiteral("完整说明见程序目录下的《用户手册.md》。"
                       "遇到问题可在「关于」页一次性导出诊断信息（日志、配置、崩溃转储）。"),
        docBox);
    tip->setTextStyle(ElaTextType::Body);
    tip->setWordWrap(true);
    docLayout->addWidget(tip);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *openManual = iconButton(QStringLiteral("打开用户手册"), docBox,
                                  ElaIconType::FileLines);
    auto *dataDirBtn = iconButton(QStringLiteral("打开数据目录"), docBox,
                                  ElaIconType::FolderOpen);
    buttons->addWidget(openManual);
    buttons->addWidget(dataDirBtn);
    buttons->addStretch(1);
    docLayout->addLayout(buttons);

    connect(openManual, &ElaPushButton::clicked, this, [this]() {
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("用户手册.md"));
        if (!QFileInfo::exists(path)) {
            ElaMessageBar::warning(ElaMessageBarType::Top, QStringLiteral("帮助"),
                                   QStringLiteral("找不到《用户手册.md》"), 2500, this);
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(dataDirBtn, &ElaPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::dataDir()));
    });

    layout->addWidget(docBox);
    layout->addStretch();
    return w;
}

// ---------------------------------------------------------------- 关于

QWidget *PreferenceDialog::buildAboutPage()
{
    QWidget *w = page();
    auto *layout = qobject_cast<QVBoxLayout *>(w->layout());

    QVBoxLayout *aboutLayout = nullptr;
    QWidget *aboutBox = groupBox(w, QStringLiteral("关于 F-Capture"), &aboutLayout);

    // 顶行：logo + 名称/版本
    auto *headerRow = new QWidget(aboutBox);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(14);

    auto *logo = new QLabel(headerRow);
    // qrc 里是 1254px 的大图，按显示尺寸平滑缩放（并考虑高 DPI）
    const int logoPx = 64;
    QPixmap logoPixmap(QStringLiteral(":/icon.png"));
    if (!logoPixmap.isNull()) {
        const qreal dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
        logoPixmap = logoPixmap.scaled(logoPx * dpr, logoPx * dpr,
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
        logoPixmap.setDevicePixelRatio(dpr);
    }
    logo->setPixmap(logoPixmap);
    logo->setFixedSize(logoPx, logoPx);
    headerLayout->addWidget(logo, 0, Qt::AlignTop);

    auto *nameLayout = new QVBoxLayout;
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(2);
    auto *title = new ElaText(QStringLiteral("F-Capture %1")
                                  .arg(QCoreApplication::applicationVersion()),
                              aboutBox);
    title->setTextStyle(ElaTextType::Title);
    nameLayout->addWidget(title);
    auto *subtitle = new ElaText(QStringLiteral("截图 · 标注 · 贴图"), aboutBox);
    subtitle->setTextStyle(ElaTextType::Caption);
    nameLayout->addWidget(subtitle);

    // 检查更新：查询 GitHub Releases 的最新 tag，与当前版本比较
    auto *updateRow = new QHBoxLayout;
    updateRow->setContentsMargins(0, 6, 0, 0);
    updateRow->setSpacing(8);
    auto *checkUpdateBtn = iconButton(QStringLiteral("检查更新"), aboutBox,
                                      ElaIconType::CloudArrowDown);
    updateRow->addWidget(checkUpdateBtn);

    m_updateInstallBtn = iconButton(QStringLiteral("立即更新"), aboutBox,
                                    ElaIconType::Download);
    m_updateInstallBtn->setVisible(false);
    updateRow->addWidget(m_updateInstallBtn);
    m_updateStatus = new ElaText(aboutBox);
    m_updateStatus->setTextStyle(ElaTextType::Caption);
    updateRow->addWidget(m_updateStatus, 1);
    nameLayout->addLayout(updateRow);
    nameLayout->addStretch(1);
    headerLayout->addLayout(nameLayout, 1);

    connect(checkUpdateBtn, &ElaPushButton::clicked, this,
            &PreferenceDialog::checkForUpdates);
    connect(m_updateInstallBtn, &ElaPushButton::clicked, this,
            &PreferenceDialog::startUpdateDownload);

    aboutLayout->addWidget(headerRow);

    auto *desc = new ElaText(
        QStringLiteral("Qt %1 · %2 · Windows x64")
            .arg(QString::fromLatin1(qVersion()),
                 AppPaths::isPortable() ? QStringLiteral("便携模式")
                                        : QStringLiteral("普通模式")),
        aboutBox);
    desc->setTextStyle(ElaTextType::Body);
    desc->setWordWrap(true);
    aboutLayout->addWidget(desc);

    auto *pathText = new ElaText(
        QStringLiteral("数据目录：%1").arg(AppPaths::dataDir()), aboutBox);
    pathText->setTextStyle(ElaTextType::Caption);
    pathText->setWordWrap(true);
    aboutLayout->addWidget(pathText);
    layout->addWidget(aboutBox);

    QVBoxLayout *diagLayout = nullptr;
    QWidget *diagBox = groupBox(w, QStringLiteral("诊断与维护"), &diagLayout);

    auto *exportRow = new QWidget(diagBox);
    auto *exportLayout = new QHBoxLayout(exportRow);
    exportLayout->setContentsMargins(0, 0, 0, 0);
    exportLayout->setSpacing(8);
    auto *folderEdit = narrowable(new ElaLineEdit(exportRow));
    folderEdit->setPlaceholderText(
        QStringLiteral("留空则导出到桌面（%1）")
            .arg(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)));
    bindString(folderEdit, QStringLiteral("diagnostics/export_folder"));
    auto *browseBtn = new ElaPushButton(QStringLiteral("浏览…"), exportRow);
    exportLayout->addWidget(folderEdit, 1);
    exportLayout->addWidget(browseBtn);
    diagLayout->addWidget(exportRow);

    connect(browseBtn, &ElaPushButton::clicked, this, [this, folderEdit]() {
        const QString start = ConfigManager::instance()
                                  ->value(QStringLiteral("diagnostics/export_folder"))
                                  .toString();
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("诊断信息导出目录"), start);
        if (dir.isEmpty())
            return;
        folderEdit->setText(dir);
        ConfigManager::instance()->setValue(QStringLiteral("diagnostics/export_folder"), dir);
    });

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *exportBtn = iconButton(QStringLiteral("导出诊断信息…"), diagBox,
                                 ElaIconType::FileExport);
    auto *logBtn = iconButton(QStringLiteral("打开日志目录"), diagBox,
                              ElaIconType::FileLines);
    auto *restartBtn = iconButton(QStringLiteral("重启应用"), diagBox,
                                  ElaIconType::ArrowsRotate);
    buttons->addWidget(exportBtn);
    buttons->addWidget(logBtn);
    buttons->addWidget(restartBtn);
    buttons->addStretch(1);
    diagLayout->addLayout(buttons);

    connect(exportBtn, &ElaPushButton::clicked, this, [this]() {
        const QString dir = ConfigManager::instance()
                                ->value(QStringLiteral("diagnostics/export_folder"))
                                .toString();
        QString error;
        const QString path = Diagnostics::exportBundle(dir, &error);
        if (path.isEmpty()) {
            ElaMessageBar::error(ElaMessageBarType::Top, QStringLiteral("导出失败"),
                                 error.isEmpty() ? QStringLiteral("无法生成诊断包") : error,
                                 3500, this);
            return;
        }
        ElaMessageBar::success(ElaMessageBarType::Top, QStringLiteral("已导出诊断信息"),
                               QDir::toNativeSeparators(path), 4000, this);
    });
    connect(logBtn, &ElaPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::logDir()));
    });
    connect(restartBtn, &ElaPushButton::clicked, this, [this]() {
        emit restartRequested();
    });

    layout->addWidget(diagBox);
    layout->addStretch();
    return w;
}

void PreferenceDialog::checkForUpdates()
{
    if (m_updateStatus) {
        m_updateStatus->setText(QStringLiteral("正在检查…"));
        m_updateStatus->setTextFormat(Qt::PlainText);
        m_updateStatus->show();
    }
    if (m_updateInstallBtn)
        m_updateInstallBtn->setVisible(false);

    if (!m_updateChecker)
        m_updateChecker = new UpdateChecker(this);

    // 每次检查重新连一次，避免重复累积连接
    disconnect(m_updateChecker, nullptr, this, nullptr);

    connect(m_updateChecker, &UpdateChecker::finished, this,
            [this](const UpdateChecker::Release &release) {
                m_pendingRelease = release;
                if (!m_updateStatus)
                    return;

                if (release.version.isEmpty()) {
                    m_updateStatus->setText(QStringLiteral("暂无可用的发布版本"));
                    return;
                }

                if (!release.available) {
                    m_updateStatus->setText(
                        QStringLiteral("已是最新版本（%1）").arg(release.version));
                    return;
                }

                if (!release.assetUrl.isValid()) {
                    // 有新版本但没有安装包：退回"打开 release 页"
                    m_updateStatus->setTextFormat(Qt::RichText);
                    m_updateStatus->setTextInteractionFlags(Qt::TextBrowserInteraction);
                    m_updateStatus->setOpenExternalLinks(true);
                    m_updateStatus->setText(
                        QStringLiteral("发现新版本 <a href=\"%1\">%2</a>，"
                                       "但该版本没有可自动更新的安装包")
                            .arg(release.pageUrl, release.version));
                    return;
                }

                m_updateStatus->setText(
                    QStringLiteral("发现新版本 %1（当前 %2）")
                        .arg(release.version,
                             QCoreApplication::applicationVersion()));
                if (m_updateInstallBtn)
                    m_updateInstallBtn->setVisible(true);
            });

    connect(m_updateChecker, &UpdateChecker::failed, this,
            [this](const QString &error) {
                if (m_updateStatus)
                    m_updateStatus->setText(
                        QStringLiteral("检查失败：%1").arg(error));
            });

    // 下载进度
    connect(m_updateChecker, &UpdateChecker::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (!m_updateStatus)
                    return;
                if (total > 0) {
                    m_updateStatus->setText(
                        QStringLiteral("正在下载更新… %1 / %2 MB")
                            .arg(received / 1024.0 / 1024.0, 0, 'f', 1)
                            .arg(total / 1024.0 / 1024.0, 0, 'f', 1));
                } else {
                    m_updateStatus->setText(
                        QStringLiteral("正在下载更新… %1 MB")
                            .arg(received / 1024.0 / 1024.0, 0, 'f', 1));
                }
            });

    connect(m_updateChecker, &UpdateChecker::downloadFinished, this,
            [this](const QString &filePath) {
                QString error;
                if (!UpdateInstaller::canInstall(&error)) {
                    if (m_updateStatus)
                        m_updateStatus->setText(error);
                    return;
                }

                // 二次确认，避免误点导致程序重启
                ElaContentDialog confirm(this);
                confirm.setWindowTitle(QStringLiteral("安装更新"));
                const QString confirmText = QStringLiteral(
                    "将关闭程序、覆盖安装并自动重启。\n"
                    "当前配置会保留，更新失败会自动回滚。\n\n继续吗？");
                auto *msg = new ElaText(confirmText, &confirm);
                msg->setTextStyle(ElaTextType::Body);
                confirm.setCentralWidget(msg);
                confirm.setLeftButtonText(QStringLiteral("取消"));
                confirm.setMiddleButtonVisible(false);
                confirm.setRightButtonText(QStringLiteral("立即更新"));
                if (confirm.exec() != QDialog::Accepted)
                    return;

                QString installError;
                if (!UpdateInstaller::installAndRestart(filePath, &installError)) {
                    if (m_updateStatus)
                        m_updateStatus->setText(installError);
                    return;
                }

                if (m_updateStatus)
                    m_updateStatus->setText(QStringLiteral("正在重启以完成更新…"));
                // 立刻退出，让外部脚本接管
                QTimer::singleShot(300, qApp, []() {
                    QCoreApplication::quit();
                });
            });

    connect(m_updateChecker, &UpdateChecker::downloadFailed, this,
            [this](const QString &error) {
                if (m_updateStatus)
                    m_updateStatus->setText(
                        QStringLiteral("下载失败：%1").arg(error));
            });

    m_updateChecker->check();
}

void PreferenceDialog::startUpdateDownload()
{
    if (!m_updateChecker || m_pendingRelease.assetUrl.isEmpty())
        return;
    if (m_updateStatus)
        m_updateStatus->setText(QStringLiteral("准备下载…"));
    m_updateChecker->download(m_pendingRelease);
}
