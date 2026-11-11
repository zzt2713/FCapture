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
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
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
        // 需要接收键盘事件（空格 = 抓手模式）
        setFocusPolicy(Qt::StrongFocus);
    }

    void setContent(const QImage &image, const QVector<WinOcr::Line> &lines)
    {
        m_image = image;
        m_lines = lines;
        m_zoom = 1.0;
        m_pan = QPointF();
        update();
    }

    // 源图的**逻辑尺寸**：line.box 就是按这个坐标系给的（相对源图左上角）。
    // 直接拿 m_image.width() 会在高 DPI 下差 dpr 倍，导致文字框整体错位。
    QSizeF sourceLogicalSize() const
    {
        if (m_image.isNull())
            return {};
        const qreal dpr = m_image.devicePixelRatio() > 0.0 ? m_image.devicePixelRatio() : 1.0;
        return QSizeF(m_image.width() / dpr, m_image.height() / dpr);
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
        // 显式给源矩形（物理像素全图）：否则 drawImage(rect, image) 会按
        // image 自带的 dpr 再缩一次，图比 target 小、四周露出底色的深灰。
        p.drawImage(target, m_image, QRectF(m_image.rect()));

        // 默认只显示原图，不叠任何框 —— 用户要先能看清识别的是哪块内容。
        // 只有**鼠标经过的行**（或点选选中的行）才画半透明蓝框提示对应位置。
        if (m_dim) {
            // 「文字高亮视图」是显式开启的模式：此时全部行都铺蓝，
            // 用来一眼看出哪些内容被识别到了。
            for (const WinOcr::Line &line : m_lines) {
                const QRectF box = mapBox(line.box, target);
                if (box.isEmpty())
                    continue;
                p.fillRect(box, QColor(0x3D, 0x8B, 0xFD, 60));
                p.setPen(QPen(QColor(0x3D, 0x8B, 0xFD, 180), 1));
                p.setBrush(Qt::NoBrush);
                p.drawRect(box);
            }
            return;
        }

        // 常态：只高亮 hover 行与已选中的行
        for (int i = 0; i < m_lines.size(); ++i) {
            const bool hovered = (i == m_hover);
            const bool selected = m_selected.contains(i);
            if (!hovered && !selected)
                continue;
            const QRectF box = mapBox(m_lines.at(i).box, target);
            if (box.isEmpty())
                continue;
            p.fillRect(box, QColor(0x3D, 0x8B, 0xFD, selected ? 70 : 45));
            p.setPen(QPen(QColor(0x3D, 0x8B, 0xFD, selected ? 200 : 140),
                          selected ? 1.5 : 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(box);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        // 空格 + 左键 = 抓手拖拽画面（放大后用来平移查看）
        if (e->button() == Qt::LeftButton && m_spaceHeld) {
            m_panning = true;
            m_panStart = e->position();
            m_panOrigin = m_pan;
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton && onSelect) {
            const int index = lineAt(e->position().toPoint());
            if (index >= 0) {
                onSelect(index, e->modifiers().testFlag(Qt::ShiftModifier),
                         e->modifiers().testFlag(Qt::ControlModifier));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_panning) {
            m_pan = m_panOrigin + (e->position() - m_panStart);
            update();
            e->accept();
            return;
        }
        if (onHover)
            onHover(lineAt(e->position().toPoint()));
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_panning && e->button() == Qt::LeftButton) {
            m_panning = false;
            setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

    // 空格按下 = 进入抓手模式（光标变张开的手），松开恢复。
    // 放在这里而不是对话框里：面板需要拿到键盘焦点，
    // 所以它自己也接受焦点（见构造函数 setFocusPolicy）。
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
            m_spaceHeld = true;
            if (!m_panning)
                setCursor(Qt::OpenHandCursor);
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }

    void keyReleaseEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
            m_spaceHeld = false;
            if (!m_panning)
                setCursor(Qt::ArrowCursor);
            e->accept();
            return;
        }
        QWidget::keyReleaseEvent(e);
    }

    // 鼠标进入时抢焦点，这样空格键能被面板收到
    void enterEvent(QEnterEvent *e) override
    {
        QWidget::enterEvent(e);
        setFocus(Qt::MouseFocusReason);
    }

    // 滚轮缩放：截图往往很小，默认已经放大到填满；滚轮可进一步放大看细节。
    void wheelEvent(QWheelEvent *e) override
    {
        const qreal step = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const qreal next = qBound(1.0, m_zoom * step, 8.0);
        if (qFuzzyCompare(next, m_zoom)) {
            e->accept();
            return;
        }
        // 以光标位置为锚点缩放，避免画面跑偏
        const QPointF cursor = e->position();
        const QRectF before = imageTarget();
        m_zoom = next;
        const QRectF after = imageTarget();
        if (!before.isEmpty()) {
            const qreal rx = (cursor.x() - before.left()) / before.width();
            const qreal ry = (cursor.y() - before.top()) / before.height();
            m_pan += QPointF(cursor.x() - (after.left() + rx * after.width()),
                             cursor.y() - (after.top() + ry * after.height()));
        }
        update();
        e->accept();
    }

    void leaveEvent(QEvent *e) override
    {
        Q_UNUSED(e);
        if (onHover)
            onHover(-1);
    }

private:
    // 图像等比铺进面板的目标矩形。
    //
    // 关键：**允许放大**。截图往往只有一两百像素高，只做"缩小适配"会把它
    // 画得极小、字形根本看不清（用户反馈"弹窗图片变小都看不清"）。
    // 这里按可用区域等比填满，小图放大、大图缩小；滚轮可再手动缩放。
    QRectF imageTarget() const
    {
        if (m_image.isNull())
            return {};
        const QSizeF logical = sourceLogicalSize();
        if (logical.isEmpty())
            return {};
        const QRectF avail = QRectF(rect()).adjusted(8, 8, -8, -8);
        qreal scale = qMin(avail.width() / logical.width(),
                           avail.height() / logical.height());
        scale *= m_zoom;              // 用户滚轮缩放（1.0 = 自动适配）
        const qreal w = logical.width() * scale;
        const qreal h = logical.height() * scale;
        // 放大到超出面板时以滚动偏移为中心裁剪
        const qreal x = avail.left() + (avail.width() - w) / 2 + m_pan.x();
        const qreal y = avail.top() + (avail.height() - h) / 2 + m_pan.y();
        return QRectF(x, y, w, h);
    }

    // 文字框是"相对源图左上角的逻辑坐标"，按图缩放映射进面板
    QRectF mapBox(const QRectF &box, const QRectF &target) const
    {
        const QSizeF logical = sourceLogicalSize();
        if (logical.isEmpty())
            return {};
        const qreal sx = target.width() / logical.width();
        const qreal sy = target.height() / logical.height();
        return QRectF(target.left() + box.left() * sx, target.top() + box.top() * sy,
                      box.width() * sx, box.height() * sy);
    }

    // 面板坐标 → 命中的文字行（取包含该点的最小框；-1 = 无）
    int lineAt(const QPoint &pos) const
    {
        const QRectF target = imageTarget();
        const QSizeF logical = sourceLogicalSize();
        if (m_image.isNull() || logical.isEmpty() || !target.contains(QPointF(pos)))
            return -1;
        const qreal sx = logical.width() / target.width();
        const qreal sy = logical.height() / target.height();
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
    qreal m_zoom = 1.0;        // 相对"自动适配"的缩放倍数
    QPointF m_pan;             // 放大后的平移偏移
    // 空格 + 左键 = 抓手拖拽（对齐常见看图软件的操作习惯）
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPointF m_panStart;
    QPointF m_panOrigin;
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

    m_retryButton = makeIconButton(QStringLiteral("重新识别"), ElaIconType::ArrowsRotate);
    connect(m_retryButton, &ElaPushButton::clicked, this, [this]() {
        if (!m_retryButton->isEnabled())
            return;
        emit retryRequested(selectedLanguage());
    });
    topRow->addWidget(m_retryButton);

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

void OcrResultDialog::setResult(const WinOcr::Result &result, const QImage &sourceImage)
{
    m_lines = result.lines;
    m_selectedLines.clear();
    m_selectionAnchor = -1;
    m_hoverLine = -1;

    if (m_imagePane)
        m_imagePane->setContent(sourceImage, m_lines);
    if (m_edit) {
        m_edit->setPlainText(result.text);
        m_edit->setExtraSelections({});
    }
    // 语言可能因为重识别而变（比如用户切了语言包），回填实际使用的语言
    if (m_languageBox && !result.language.isEmpty()) {
        const int idx = m_languageBox->findText(result.language);
        if (idx >= 0)
            m_languageBox->setCurrentIndex(idx);
    }
    setRetryBusy(false);
}

void OcrResultDialog::setRetryBusy(bool busy)
{
    if (!m_retryButton)
        return;
    m_retryButton->setEnabled(!busy);
    m_retryButton->setText(busy ? QStringLiteral("识别中…")
                                : QStringLiteral("重新识别"));
}

QString OcrResultDialog::text() const
{
    return m_edit ? m_edit->toPlainText() : QString();
}

QString OcrResultDialog::selectedLanguage() const
{
    return m_languageBox ? m_languageBox->currentText() : QString();
}
