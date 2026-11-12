#include "ui/ShortcutConfigDialog.h"

#include "ui/ShortcutConfigWidget.h"

#include "ElaPushButton.h"
#include "ElaTheme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {

constexpr int kModalWidth = 520;
constexpr int kRadius = 10;
constexpr int kPaddingH = 24;
constexpr int kPaddingTop = 20;
constexpr int kPaddingBottom = 20;
constexpr int kTitleGap = 16;
constexpr int kButtonHeight = 32;
constexpr int kButtonGap = 8;
constexpr int kCloseSize = 24;
constexpr int kTitleFontSize = 16;

QColor themeColor(ElaThemeType::ThemeColor role)
{
    return eTheme->getThemeColor(eTheme->getThemeMode(), role);
}

class CloseButton final : public QWidget
{
public:
    explicit CloseButton(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(kCloseSize, kCloseSize);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        if (m_hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(themeColor(ElaThemeType::BasicHover));
            p.drawRoundedRect(rect(), 4, 4);
        }
        const QColor fg = m_hover ? themeColor(ElaThemeType::BasicText)
                                  : themeColor(ElaThemeType::BasicDetailsText);
        p.setPen(QPen(fg, 1.4, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = QRectF(rect()).center();
        const qreal d = 4.5;
        p.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        p.drawLine(QPointF(c.x() + d, c.y() - d), QPointF(c.x() - d, c.y() + d));
    }

    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClick)
            onClick();
    }

private:
    bool m_hover = false;
};

} // namespace

ShortcutConfigDialog::ShortcutConfigDialog(const QString &actionLabel,
                                           const QStringList &currentKeys,
                                           QWidget *parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(kPaddingH, kPaddingTop, kPaddingH, kPaddingBottom);
    root->setSpacing(0);

    // 标题行
    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(0);

    m_titleLabel = new QLabel(QStringLiteral("\"%1\"的按键配置").arg(actionLabel), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(kTitleFontSize);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(
        QStringLiteral("background: transparent; color: %1;")
            .arg(themeColor(ElaThemeType::BasicText).name(QColor::HexArgb)));
    headerRow->addWidget(m_titleLabel, 1);

    auto *closeButton = new CloseButton(this);
    closeButton->onClick = [this]() { reject(); };
    headerRow->addWidget(closeButton, 0, Qt::AlignTop);

    root->addLayout(headerRow);
    root->addSpacing(kTitleGap);

    // 内容
    m_content = new ShortcutConfigWidget(currentKeys, this);
    root->addWidget(m_content, 1);
    root->addSpacing(kTitleGap);

    // footer
    auto *footerRow = new QHBoxLayout;
    footerRow->setContentsMargins(0, 0, 0, 0);
    footerRow->setSpacing(kButtonGap);
    footerRow->addStretch(1);

    m_rejectButton = new ElaPushButton(QStringLiteral("取消"), this);
    m_rejectButton->setFixedHeight(kButtonHeight);
    m_rejectButton->setBorderRadius(6);
    m_rejectButton->setMinimumWidth(76);
    m_rejectButton->setCursor(Qt::PointingHandCursor);

    m_acceptButton = new ElaPushButton(QStringLiteral("确定"), this);
    m_acceptButton->setFixedHeight(kButtonHeight);
    m_acceptButton->setBorderRadius(6);
    m_acceptButton->setMinimumWidth(76);
    m_acceptButton->setCursor(Qt::PointingHandCursor);
    m_acceptButton->setLightDefaultColor(themeColor(ElaThemeType::PrimaryNormal));
    m_acceptButton->setLightHoverColor(themeColor(ElaThemeType::PrimaryHover));
    m_acceptButton->setLightPressColor(themeColor(ElaThemeType::PrimaryPress));
    m_acceptButton->setLightTextColor(Qt::white);
    m_acceptButton->setDarkDefaultColor(themeColor(ElaThemeType::PrimaryNormal));
    m_acceptButton->setDarkHoverColor(themeColor(ElaThemeType::PrimaryHover));
    m_acceptButton->setDarkPressColor(themeColor(ElaThemeType::PrimaryPress));
    m_acceptButton->setDarkTextColor(Qt::black);

    footerRow->addWidget(m_rejectButton, 0);
    footerRow->addWidget(m_acceptButton, 0);
    root->addLayout(footerRow);

    connect(m_rejectButton, &ElaPushButton::clicked, this, &QDialog::reject);
    connect(m_acceptButton, &ElaPushButton::clicked, this, [this]() {
        if (m_content.isNull() || !m_content->canAccept())
            return;
        m_content->commitPending();
        m_result = m_content->keys();
        accept();
    });
    connect(m_content, &ShortcutConfigWidget::stateChanged,
            this, &ShortcutConfigDialog::syncAcceptEnabled);
    syncAcceptEnabled();

    resize(kModalWidth, 300);

    // 应用级拦截键盘：不依赖焦点 / grabKeyboard
    qApp->installEventFilter(this);
}

ShortcutConfigDialog::~ShortcutConfigDialog()
{
    qApp->removeEventFilter(this);
}

QStringList ShortcutConfigDialog::keys() const
{
    return m_result;
}

void ShortcutConfigDialog::setValidator(std::function<QString(const QString &key)> validator)
{
    if (m_content.isNull())
        return;
    m_content->setValidator(std::move(validator));
}

void ShortcutConfigDialog::syncAcceptEnabled()
{
    if (m_acceptButton == nullptr || m_content.isNull())
        return;
    m_acceptButton->setEnabled(m_content->canAccept());
}

bool ShortcutConfigDialog::eventFilter(QObject *watched, QEvent *event)
{
    // 弹窗已经不可见 / 内容已销毁 → 直接放行，不做任何访问。
    // 应用级过滤器在弹窗析构前仍然生效，此时子控件可能已被销毁，
    // 直接解引用 m_content 会 0xC0000005。
    if (!isVisible() || m_content.isNull())
        return QDialog::eventFilter(watched, event);

    if (!m_content->isRecording())
        return QDialog::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::ShortcutOverride:
        event->accept();
        return true;
    case QEvent::KeyPress:
        m_content->forwardKeyPress(static_cast<QKeyEvent *>(event));
        event->accept();
        return true;
    case QEvent::KeyRelease:
        event->accept();
        return true;
    default:
        return QDialog::eventFilter(watched, event);
    }
}

void ShortcutConfigDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF panel = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(panel, kRadius, kRadius);
    p.fillPath(path, themeColor(ElaThemeType::DialogBase));
    p.setPen(QPen(themeColor(ElaThemeType::BasicBorder), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ShortcutConfigDialog::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QDialog::mousePressEvent(e);
}

void ShortcutConfigDialog::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging)
        move(e->globalPosition().toPoint() - m_dragOffset);
    QDialog::mouseMoveEvent(e);
}

void ShortcutConfigDialog::mouseReleaseEvent(QMouseEvent *e)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(e);
}

void ShortcutConfigDialog::keyPressEvent(QKeyEvent *e)
{
    if (!m_content.isNull() && !m_content->isRecording() && e->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(e);
}
