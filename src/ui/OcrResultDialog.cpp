#include "ui/OcrResultDialog.h"

#include "ElaComboBox.h"
#include "ElaPlainTextEdit.h"
#include "ElaPushButton.h"
#include "ElaText.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

// 识别结果的图像面板：原图 + 每行文字框。hover / 点选经回调通知外层
//（匿名命名空间里不能放 Q_OBJECT，这里用 std::function 回调代替信号）。
class OcrImagePane : public QWidget
{
public:
    using HoverCallback = std::function<void(int)>;
    using SelectCallback = std::function<void(int, bool, bool)>;

    explicit OcrImagePane(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setMinimumWidth(240);
    }

    void setContent(const QImage &image, const QVector<WinOcr::Line> &lines)
    {
        m_image = image;
        m_lines = lines;
        update();
    }

    void setHoverLine(int index)
    {
        if (m_hover != index) {
            m_hover = index;
            update();
        }
    }

    void setSelectedLines(const QSet<int> &rows)
    {
        if (m_selected != rows) {
            m_selected = rows;
            update();
        }
    }

    void setDimMode(bool on)
    {
        if (m_dim != on) {
            m_dim = on;
            update();
        }
    }

    HoverCallback onHover;
    SelectCallback onSelect;

protected:
    void paintEvent(QPaintEvent *e) override
    {
        Q_UNUSED(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        p.fillRect(rect(), QColor(0x24, 0x24, 0x24));
        if (m_image.isNull())
            return;

        const QRectF target = imageTarget();
        p.drawImage(target, m_image);

        // 文字高亮视图（snow 的 textRegions 相减同款效果）：
        // 非文字区压暗、文字框内透出原图
        if (m_dim) {
            p.fillRect(target, QColor(0, 0, 0, 150));
            for (const WinOcr::Line &line : m_lines) {
                const QRectF box = mapBox(line.box, target);
                p.save();
                p.setClipRect(box);
                p.drawImage(target, m_image);
                p.restore();
            }
        }

        // 文字框：hover / 选中加粗加亮
        for (int i = 0; i < m_lines.size(); ++i) {
            const QRectF box = mapBox(m_lines.at(i).box, target);
            const bool hot = (i == m_hover) || m_selected.contains(i);
            p.setPen(QPen(QColor(0x3D, 0x8B, 0xFD), hot ? 1.5 : 1));
            p.setBrush(hot ? QColor(0x3D, 0x8B, 0xFD, 40) : Qt::NoBrush);
            p.drawRect(box);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (onHover)
            onHover(lineAt(e->position().toPoint()));
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onSelect) {
            const int index = lineAt(e->position().toPoint());
            if (index >= 0) {
                onSelect(index, e->modifiers().testFlag(Qt::ShiftModifier),
                         e->modifiers().testFlag(Qt::ControlModifier));
            }
        }
    }

    void leaveEvent(QEvent *e) override
    {
        Q_UNUSED(e);
        if (onHover)
            onHover(-1);
    }

private:
    // 图像等比缩放进面板的目标矩形
    QRectF imageTarget() const
    {
        if (m_image.isNull())
            return {};
        const QRectF avail = QRectF(rect()).adjusted(8, 8, -8, -8);
        const qreal scale = qMin(avail.width() / m_image.width(),
                                avail.height() / m_image.height());
        const qreal w = m_image.width() * scale;
        const qreal h = m_image.height() * scale;
        return QRectF(avail.left() + (avail.width() - w) / 2,
                      avail.top() + (avail.height() - h) / 2, w, h);
    }

    // 文字框是"相对源图左上角的逻辑坐标"，按图缩放映射进面板
    QRectF mapBox(const QRectF &box, const QRectF &target) const
    {
        const qreal sx = target.width() / m_image.width();
        const qreal sy = target.height() / m_image.height();
        return QRectF(target.left() + box.left() * sx, target.top() + box.top() * sy,
                      box.width() * sx, box.height() * sy);
    }

    // 面板坐标 → 命中的文字行（取包含该点的最小框；-1 = 无）
    int lineAt(const QPoint &pos) const
    {
        const QRectF target = imageTarget();
        if (m_image.isNull() || !target.contains(QPointF(pos)))
            return -1;
        const qreal sx = m_image.width() / target.width();
        const qreal sy = m_image.height() / target.height();
        const QPointF imgPos((pos.x() - target.left()) * sx, (pos.y() - target.top()) * sy);
        int best = -1;
        qreal bestArea = 0.0;
        for (int i = 0; i < m_lines.size(); ++i) {
            const QRectF &box = m_lines.at(i).box;
            if (!box.contains(imgPos))
                continue;
            const qreal area = box.width() * box.height();
            if (best < 0 || area < bestArea) {
                best = i;
                bestArea = area;
            }
        }
        return best;
    }

    QImage m_image;
    QVector<WinOcr::Line> m_lines;
    QSet<int> m_selected;
    int m_hover = -1;
    bool m_dim = false;
};

OcrResultDialog::OcrResultDialog(const WinOcr::Result &result, const QStringList &languages,
                                 const QString &currentLanguage, const QImage &sourceImage,
                                 QWidget *parent)
    : ElaDialog(parent)
    , m_lines(result.lines)
{
    setWindowTitle(QStringLiteral("识别文字"));
    setMinimumSize(720, 480);
    resize(920, 600);
    setWindowButtonFlag(ElaAppBarType::MaximizeButtonHint, false);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 12, 20, 16);
    layout->setSpacing(12);

    // ---- 顶部：复制 / 高亮视图 / 语言 ----
    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(10);

    // ElaPushButton 的 minimumSizeHint 不含图标宽度，不显式给最小宽度会把图标裁掉
    const auto makeIconButton = [this](const QString &text, ElaIconType::IconName icon) {
        auto *btn = new ElaPushButton(text, this);
        btn->setElaIcon(icon);
        const QFontMetrics fm(btn->font());
        btn->setMinimumWidth(fm.horizontalAdvance(text) + 64);
        btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        return btn;
    };

    auto *copyAll = makeIconButton(QStringLiteral("复制全文"), ElaIconType::Copy);
    connect(copyAll, &ElaPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(this->text());
    });
    topRow->addWidget(copyAll);

    auto *copySel = makeIconButton(QStringLiteral("复制选中"), ElaIconType::Copy);
    connect(copySel, &ElaPushButton::clicked, this, &OcrResultDialog::copySelected);
    topRow->addWidget(copySel);

    auto *dimBtn = makeIconButton(QStringLiteral("文字高亮视图"), ElaIconType::ArrowsToEye);
    connect(dimBtn, &ElaPushButton::clicked, this, [this, dimBtn]() {
        m_dimView = !m_dimView;
        m_imagePane->setDimMode(m_dimView);
        dimBtn->setText(m_dimView ? QStringLiteral("退出高亮视图")
                                  : QStringLiteral("文字高亮视图"));
    });
    topRow->addWidget(dimBtn);

    auto *retry = makeIconButton(QStringLiteral("重新识别"), ElaIconType::ArrowsRotate);
    connect(retry, &ElaPushButton::clicked, this, [this]() {
        emit retryRequested(selectedLanguage());
    });
    topRow->addWidget(retry);

    topRow->addStretch();

    topRow->addWidget(new ElaText(QStringLiteral("语言"), this));
    m_languageBox = new ElaComboBox(this);
    m_languageBox->addItems(languages);
    if (!currentLanguage.isEmpty()) {
        const int idx = m_languageBox->findText(currentLanguage);
        if (idx >= 0)
            m_languageBox->setCurrentIndex(idx);
    }
    m_languageBox->setMinimumWidth(180);
    topRow->addWidget(m_languageBox);

    layout->addLayout(topRow);

    // ---- 主体：左图右文 ----
    auto *split = new QHBoxLayout;
    split->setSpacing(12);

    m_imagePane = new OcrImagePane(this);
    m_imagePane->setContent(sourceImage, m_lines);
    m_imagePane->onHover = [this](int index) { setHoverLine(index); };
    m_imagePane->onSelect = [this](int index, bool shift, bool ctrl) {
        if (shift && m_selectionAnchor >= 0) {
            // Shift：从锚点到当前行连片增选
            const int lo = qMin(m_selectionAnchor, index);
            const int hi = qMax(m_selectionAnchor, index);
            for (int i = lo; i <= hi; ++i)
                m_selectedLines.insert(i);
        } else if (ctrl) {
            if (m_selectedLines.contains(index))
                m_selectedLines.remove(index);
            else
                m_selectedLines.insert(index);
            m_selectionAnchor = index;
        } else {
            m_selectedLines = { index };
            m_selectionAnchor = index;
            setHoverLine(index);
        }
        m_imagePane->setSelectedLines(m_selectedLines);
        refreshHighlights();
    };
    split->addWidget(m_imagePane, 55);

    m_edit = new ElaPlainTextEdit(this);
    m_edit->setPlainText(result.text);
    split->addWidget(m_edit, 45);
    layout->addLayout(split, 1);

    // 文本区 hover → 图上文字框联动
    m_edit->viewport()->installEventFilter(this);

    auto *hint = new ElaText(
        QStringLiteral("悬停图文联动   ·   单击文字框选中该行   ·   Shift / Ctrl 多选"), this);
    layout->addWidget(hint);
}

bool OcrResultDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QTextCursor cursor = m_edit->cursorForPosition(me->position().toPoint());
            setHoverLine(cursor.blockNumber());
        } else if (event->type() == QEvent::Leave) {
            setHoverLine(-1);
        }
    }
    return ElaDialog::eventFilter(watched, event);
}

void OcrResultDialog::setHoverLine(int index)
{
    if (m_hoverLine == index)
        return;
    m_hoverLine = index;
    m_imagePane->setHoverLine(index);
    refreshHighlights();
}

void OcrResultDialog::refreshHighlights()
{
    if (!m_edit)
        return;

    QList<QTextEdit::ExtraSelection> extras;
    const auto addBlockBg = [&extras, this](int blockNumber, const QColor &color) {
        const QTextBlock block = m_edit->document()->findBlockByNumber(blockNumber);
        if (!block.isValid())
            return;
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(color);
        sel.cursor = QTextCursor(block);
        sel.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        extras.append(sel);
    };

    for (int i : m_selectedLines)
        addBlockBg(i, QColor(0x3D, 0x8B, 0xFD, 72));
    if (m_hoverLine >= 0)
        addBlockBg(m_hoverLine, QColor(0x3D, 0x8B, 0xFD, 40));

    m_edit->setExtraSelections(extras);
}

QString OcrResultDialog::selectedLinesText() const
{
    if (!m_edit)
        return {};
    QList<int> rows = m_selectedLines.values();
    std::sort(rows.begin(), rows.end());
    QStringList out;
    for (int i : rows) {
        const QTextBlock block = m_edit->document()->findBlockByNumber(i);
        if (block.isValid())
            out.append(block.text());
    }
    return out.join(QLatin1Char('\n'));
}

void OcrResultDialog::copySelected()
{
    QString picked;
    if (m_edit && m_edit->textCursor().hasSelection())
        picked = m_edit->textCursor().selectedText();
    else
        picked = selectedLinesText();
    picked.replace(QChar(0x2029), QLatin1Char('\n'));   // 段落分隔符 → 换行
    if (!picked.isEmpty())
        QApplication::clipboard()->setText(picked);
}

QString OcrResultDialog::text() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

QString OcrResultDialog::selectedLanguage() const
{
    return m_languageBox ? m_languageBox->currentText() : QString();
}
