#include "snip/SizeInputBox.h"

#include <QHBoxLayout>
#include <QIntValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>

namespace {

constexpr int kMinSide = 1;
constexpr int kMaxSide = 20000;

QLineEdit *makeEdit(QWidget *parent, int value)
{
    auto *edit = new QLineEdit(parent);
    edit->setValidator(new QIntValidator(kMinSide, kMaxSide, edit));
    edit->setText(QString::number(value));
    edit->setFixedWidth(56);
    edit->setAlignment(Qt::AlignCenter);
    return edit;
}

} // namespace

SizeInputBox::SizeInputBox(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(4);

    m_widthEdit = makeEdit(this, 0);
    m_heightEdit = makeEdit(this, 0);

    auto *sep = new QLabel(QStringLiteral("×"), this);
    sep->setStyleSheet(QStringLiteral("color: #8A8A8A;"));

    layout->addWidget(m_widthEdit);
    layout->addWidget(sep);
    layout->addWidget(m_heightEdit);

    for (QLineEdit *edit : {m_widthEdit, m_heightEdit})
        edit->installEventFilter(this);

    hide();
}

void SizeInputBox::startEditing(const QSize &currentSize, const QString &unit)
{
    m_done = false;
    m_widthEdit->setText(QString::number(currentSize.width()));
    m_heightEdit->setText(QString::number(currentSize.height()));
    setToolTip(unit);

    adjustSize();
    resize(sizeHint());
    show();
    raise();

    m_widthEdit->setFocus(Qt::OtherFocusReason);
    m_widthEdit->selectAll();
}

void SizeInputBox::commit()
{
    if (m_done)
        return;
    m_done = true;

    const int w = qBound(kMinSide, m_widthEdit->text().toInt(), kMaxSide);
    const int h = qBound(kMinSide, m_heightEdit->text().toInt(), kMaxSide);
    hide();
    emit committed(QSize(w, h));
    deleteLater();
}

void SizeInputBox::cancel()
{
    if (m_done)
        return;
    m_done = true;

    hide();
    emit cancelled();
    deleteLater();
}

void SizeInputBox::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0x3D, 0x3D, 0x3D), 1));
    p.setBrush(QColor(0x2B, 0x2B, 0x2B));
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 4, 4);
}

bool SizeInputBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_widthEdit && watched != m_heightEdit)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Escape:
            cancel();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            commit();
            return true;
        case Qt::Key_Tab: {
            QLineEdit *other = (watched == m_widthEdit) ? m_heightEdit : m_widthEdit;
            other->setFocus(Qt::TabFocusReason);
            other->selectAll();
            return true;
        }
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}
