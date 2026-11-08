#include "annotate/TextInputBox.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kPadding = 4;
constexpr int kMinWidth = 60;
constexpr int kMaxWidth = 4000;

} // namespace

TextInputBox::TextInputBox(QWidget *parent)
    : QWidget(parent)
{
    // 子控件：不做顶层窗口，避免和截图层抢焦点
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(kPadding, kPadding, kPadding, kPadding);
    layout->setSpacing(0);

    m_edit = new QTextEdit(this);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_edit->setAcceptRichText(false);
    m_edit->setLineWrapMode(QTextEdit::WidgetWidth);
    m_edit->setTabChangesFocus(false);
    m_edit->installEventFilter(this);
    layout->addWidget(m_edit);

    // 完全透明：你看到的就是最终效果
    m_edit->setStyleSheet(QStringLiteral(
        "QTextEdit {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  margin: 0px;"
        "  selection-background-color: rgba(61,139,253,140);"
        "}"));

    hide();
}

void TextInputBox::startEditing(const QPoint &localPos, const QColor &color, int fontSize,
                                const QString &initial)
{
    m_anchor = localPos;
    m_fontSize = qBound(6, fontSize, 400);
    m_color = color;
    m_done = false;
    m_hasFocusOnce = false;

    // 重置幂等缓存：新会话必须重新应用一次尺寸
    m_lastEditWidth = -1;
    m_lastEditHeight = -1;
    m_lastBoxW = -1;
    m_lastBoxH = -1;

    // 先清空/填充内容，再套格式：applyFont 会遍历文档改字符格式，
    // 顺序反过来会被 clear()/setPlainText() 冲掉
    m_edit->clear();
    if (!initial.isEmpty()) {
        m_edit->setPlainText(initial);
        m_edit->selectAll();
    }

    applyFont();
    if (!initial.isEmpty())
        m_edit->selectAll();

    relayout();
    move(m_anchor);
    show();
    raise();

    // 子控件拿焦点不需要 activateWindow，但焦点事件是异步的，下一轮再设。
    // 拿到焦点后必须再刷一次颜色：QTextEdit 在获得焦点/光标重建时
    // 会把 currentCharFormat 重置成默认值（黑色），这就是"输入时白/黑、
    // 失焦后才变选定色"的根因。
    QMetaObject::invokeMethod(this, [this]() {
        m_edit->setFocus(Qt::MouseFocusReason);
        m_edit->moveCursor(QTextCursor::End);

        // 焦点建立后再锁一次颜色，防止光标重建把格式冲回默认黑
        m_edit->setTextColor(m_color);
        QTextCharFormat fmt;
        fmt.setForeground(m_color);
        m_edit->mergeCurrentCharFormat(fmt);
    }, Qt::QueuedConnection);
}

void TextInputBox::applyFont()
{
    if (!m_edit)
        return;

    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPixelSize(m_fontSize);
    m_edit->setFont(f);
    m_edit->document()->setDocumentMargin(0);

    // 颜色写进"当前字符格式"，接下来键入的每个字都会带上它。
    //
    // 坑：文档为空时 mergeCurrentCharFormat 不生效（没有可合并的 fragment），
    // 新输入的字会退回默认黑色。所以要先 setTextColor 建立光标格式，
    // 再 merge 一次把整份文档（如果已有内容）也刷成同色。
    QTextCharFormat fmt;
    fmt.setForeground(m_color);

    m_edit->setTextColor(m_color);      // 建立光标处格式（空文档时唯一有效的途径）
    m_edit->mergeCurrentCharFormat(fmt);

    // 已有内容（预填文字）也要刷成这个颜色
    const QString content = m_edit->toPlainText();
    if (!content.isEmpty()) {
        QTextCursor cur(m_edit->document());
        cur.select(QTextCursor::Document);
        cur.mergeCharFormat(fmt);
    }
}

// 合并重排请求：一次输入法事件会连发 KeyPress/KeyRelease/InputMethod，
// 逐个排队会在事件循环里堆积大量 relayout，中文输入时尤其明显。
// 用一个标志保证同一轮事件循环里最多执行一次。
void TextInputBox::scheduleRelayout()
{
    if (m_relayoutPending)
        return;
    m_relayoutPending = true;

    QMetaObject::invokeMethod(this, [this]() {
        m_relayoutPending = false;
        relayout();
    }, Qt::QueuedConnection);
}

void TextInputBox::relayout()
{
    if (!m_edit)
        return;

    const QFontMetricsF fm(m_edit->font());
    const QString content = m_edit->toPlainText();

    qreal widest = 0;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        widest = qMax(widest, fm.horizontalAdvance(line));

    int w = int(qBound<qreal>(kMinWidth, widest + fm.averageCharWidth() * 2, kMaxWidth));

    // 用自己记录的"上次设定值"判断，不能用 m_edit->width()：
    // setFixedWidth 设的是外部尺寸，控件实际 width() 受边框/布局影响，
    // 两者永远不相等，会导致每次 relayout 都重复 setFixedWidth → 无限重排爆栈。
    if (m_lastEditWidth != w) {
        m_lastEditWidth = w;
        m_edit->setFixedWidth(w);
        m_edit->document()->setTextWidth(w);
    }
    m_edit->document()->size();   // 强制完成布局

    qreal total = 0;
    for (QTextBlock block = m_edit->document()->begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout)
            continue;
        for (int i = 0; i < layout->lineCount(); ++i)
            total += layout->lineAt(i).height();
    }
    const int h = qMax(1, int(total > 0 ? total : fm.lineSpacing()));

    if (m_lastEditHeight != h) {
        m_lastEditHeight = h;
        m_edit->setFixedHeight(h);
    }

    const int newW = w + kPadding * 2;
    const int newH = h + kPadding * 2;
    if (m_lastBoxW != newW || m_lastBoxH != newH) {
        m_lastBoxW = newW;
        m_lastBoxH = newH;
        resize(newW, newH);
    }

    // 不超出父控件
    if (parentWidget()) {
        const QRect avail = parentWidget()->rect();
        if (x() + width() > avail.right())
            move(qMax(0, avail.right() - width()), y());
        if (y() + height() > avail.bottom())
            move(x(), qMax(0, avail.bottom() - height()));
    }
}

QString TextInputBox::text() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

void TextInputBox::commit()
{
    if (m_done)
        return;
    m_done = true;

    const QString t = text();
    hide();
    emit committed(t);
    deleteLater();
}

void TextInputBox::cancel()
{
    if (m_done)
        return;
    m_done = true;

    hide();
    emit cancelled();
    deleteLater();
}

void TextInputBox::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        cancel();
        return;
    }
    QWidget::keyPressEvent(e);
}

void TextInputBox::wheelEvent(QWheelEvent *e)
{
    const int delta = e->angleDelta().y() > 0 ? 1 : -1;
    m_fontSize = qBound(6, m_fontSize + delta, 400);
    applyFont();
    relayout();
    emit fontResized(m_fontSize);
    e->accept();
}

bool TextInputBox::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_edit)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::KeyPress: {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && ke->modifiers().testFlag(Qt::ControlModifier)) {
            commit();
            return true;
        }
        scheduleRelayout();
        return false;
    }
    case QEvent::InputMethod:
    case QEvent::KeyRelease:
        scheduleRelayout();
        return false;

    case QEvent::FocusIn:
        m_hasFocusOnce = true;
        return false;

    case QEvent::FocusOut:
        // 只有真正拿到过焦点、且是用户主动切走时才提交。
        // 在父窗口内部转移焦点不算"点外部"。
        if (m_hasFocusOnce && !m_done) {
            QMetaObject::invokeMethod(this, [this]() {
                if (m_done)
                    return;
                // 焦点仍在输入框内部（比如点到它的边框）就不提交
                QWidget *fw = QApplication::focusWidget();
                if (fw && (fw == this || isAncestorOf(fw)))
                    return;
                commit();
            }, Qt::QueuedConnection);
        }
        return false;

    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void TextInputBox::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(QColor(0x3D, 0x8B, 0xFD, 200), 1, Qt::DashLine);
    pen.setDashPattern({3, 3});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
}
