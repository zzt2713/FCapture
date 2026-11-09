#include "annotate/shapes/TextShape.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>

TextShape::TextShape()
{
    m_fontSize = 16;
    m_penWidth = 16;   // 文字用 penWidth 同步字号
}

Annotation::Type TextShape::type() const
{
    return Type::Text;
}

QFont TextShape::font() const
{
    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPixelSize(m_fontSize);
    return f;
}

void TextShape::setText(const QString &t)
{
    m_text = t;
}

void TextShape::setPosition(const QPointF &pos)
{
    m_pos = pos;
}

void TextShape::setFontSize(int px)
{
    m_fontSize = qBound(6, px, 400);
}

// 唯一的排版来源：所有测量和绘制都走这一个 document，保证边界框与实际绘制一致
QTextDocument *TextShape::createDocument() const
{
    auto *doc = new QTextDocument();
    doc->setDefaultFont(font());
    doc->setPlainText(m_text);
    doc->setDocumentMargin(0);

    if (m_boxWidth > 0) {
        doc->setTextWidth(m_boxWidth);
        QTextOption opt = doc->defaultTextOption();
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc->setDefaultTextOption(opt);
    } else {
        // 不设宽度时不换行，用 idealWidth 作为自然宽度
        doc->setTextWidth(-1);
    }

    // 颜色通过字符格式设置
    QTextCursor cursor(doc);
    cursor.select(QTextCursor::Document);
    QTextCharFormat fmt;
    fmt.setForeground(m_color);
    cursor.mergeCharFormat(fmt);

    return doc;
}

QRectF TextShape::layoutRect() const
{
    if (m_text.isEmpty())
        return QRectF(m_pos, QSizeF(0, 0));

    QTextDocument *doc = createDocument();
    const QFontMetricsF fm(font());

    qreal width = 0;
    qreal height = 0;

    if (m_boxWidth <= 0) {
        // 不换行：宽度按最长行精确测量，高度按实际行数
        const QStringList lines = m_text.split(QLatin1Char('\n'));
        for (const QString &line : lines)
            width = qMax(width, fm.horizontalAdvance(line));
        width = qMax<qreal>(1.0, width);

        // 空行也要占一行高度
        const int lineCount = qMax(1, int(lines.size()));
        height = fm.lineSpacing() * lineCount;
    } else {
        // 固定宽度：宽度就是 m_boxWidth，高度按实际折行数算
        width = m_boxWidth;

        // 关键：先访问一次 size() 强制 QTextDocument 完成布局，
        // 否则 block.layout()->lineCount() 还是布局前的旧值，会少算行数。
        doc->size();

        qreal total = 0;
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            const QTextLayout *layout = block.layout();
            if (!layout)
                continue;
            for (int i = 0; i < layout->lineCount(); ++i)
                total += layout->lineAt(i).height();
        }
        height = (total > 0) ? total : fm.lineSpacing();
    }

    delete doc;
    return QRectF(m_pos, QSizeF(width, height));
}

QRectF TextShape::rawBoundingRect() const
{
    return layoutRect().adjusted(-2, -2, 2, 2);
}

QPainterPath TextShape::buildGlyphPath() const
{
    QPainterPath path;
    if (m_text.isEmpty())
        return path;

    QTextDocument *doc = createDocument();
    doc->size();

    // 按行取字形的真实位置：基线 = 块位置 + 行 y + 行 ascent
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout)
            continue;

        const QPointF blockPos = doc->documentLayout()->blockBoundingRect(block).topLeft();
        const int blockStart = block.position();

        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int start = blockStart + line.textStart();
            const int end = start + line.textLength();
            const qreal baseline = blockPos.y() + line.y() + line.ascent();

            for (int pos = start; pos < end && pos < m_text.size();) {
                const QChar ch = m_text.at(pos);
                const int len = ch.isHighSurrogate() ? 2 : 1;
                const qreal x = blockPos.x() + line.cursorToX(pos);
                path.addText(x, baseline, font(), m_text.mid(pos, len));
                pos += len;
            }
        }
    }

    delete doc;
    return path;
}

void TextShape::paintContent(QPainter *p) const
{
    if (m_text.isEmpty())
        return;

    const QRectF r = layoutRect();

    p->save();
    p->setRenderHint(QPainter::TextAntialiasing);

    // 背景填充（默认透明）
    if (m_bgColor.alpha() > 0) {
        p->setPen(Qt::NoPen);
        p->setBrush(m_bgColor);
        p->drawRoundedRect(r.adjusted(-3, -2, 3, 2), 3, 3);
    }

    // 描边：先用字形轮廓描一圈，再正常绘制文字覆盖在上面
    if (m_outline && m_outlineWidth > 0) {
        QPainterPath path = buildGlyphPath();
        if (!path.isEmpty()) {
            p->translate(r.topLeft());
            // 描边宽度取两倍：一半被上方填充色盖住，露出的正好是设定宽度
            p->setPen(QPen(m_outlineColor, m_outlineWidth * 2.0,
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            p->drawPath(path);
            p->translate(-r.topLeft());
        }
    }

    QTextDocument *doc = createDocument();

    // 绘制时不要限制裁剪高度：doc 的实际高度可能比 r.height() 略大（行距取整差异），
    // 裁掉就会丢最后一行。用 doc 自己的尺寸作为裁剪区。
    p->translate(r.topLeft());
    doc->drawContents(p, QRectF(0, 0, r.width() + 1, doc->size().height() + 1));

    delete doc;
    p->restore();
}

bool TextShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    return layoutRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(pt);
}

void TextShape::translate(const QPointF &delta)
{
    m_pos += delta;
}

QVector<QPointF> TextShape::rawHandles() const
{
    // 与矩形/椭圆一致的 8 个手柄：
    //   角（0/2/4/6）      → 等比缩放字号
    //   左右边（3/7）      → 改文本框宽度（自动换行）
    //   上下边（1/5）      → 同样按字号缩放入口处理（文本框高度由内容决定）
    const QRectF r = layoutRect();
    return {
        r.topLeft(), QPointF(r.center().x(), r.top()), r.topRight(),
        QPointF(r.right(), r.center().y()), r.bottomRight(),
        QPointF(r.center().x(), r.bottom()), r.bottomLeft(),
        QPointF(r.left(), r.center().y())
    };
}

void TextShape::beginHandleDrag()
{
    // 快照当前状态作为本次拖拽的固定基准。
    // 关键：不要用"相对当前值再乘比例"，那样每移动 1px 就再乘一次，
    // 拖几下字号就爆到几百、反向也缩不回去。
    m_dragBaseFontSize = m_fontSize;
    m_dragBaseBoxWidth = m_boxWidth;
    m_dragBaseRect = layoutRect();
    m_hasDragBaseline = true;
}

void TextShape::moveHandleRaw(int index, const QPointF &pos)
{
    if (!m_hasDragBaseline)
        beginHandleDrag();

    const QRectF r = m_dragBaseRect;
    if (r.width() <= 0 || r.height() <= 0)
        return;

    switch (index) {
    // 左右边：改文本框宽度（超出宽度后自动换行），字号不变。
    // 左拉宽 / 右拉窄都支持；最小 40px，避免缩成一条线。
    case 3:
    case 7: {
        const qreal w = (index == 3) ? pos.x() - m_pos.x() : m_pos.x() - pos.x();
        m_boxWidth = qMax<qreal>(40, w);
        return;
    }

    // 角 + 上下边：相对固定锚点做等比缩字号，比例可大于也可小于 1
    default: {
        const QPointF anchor = [&]() -> QPointF {
            switch (index) {
            case 0: return r.bottomRight();
            case 1: return r.bottomLeft();
            case 2: return r.bottomLeft();
            case 4: return r.topLeft();
            case 5: return r.topRight();
            case 6: return r.topRight();
            default: return r.center();
            }
        }();

        // 用"离锚点多远"的比值：往外拖 >1 放大，往里拖 <1 缩小。
        // 基准固定为拖拽开始时的矩形，因此过程线性、可反向。
        const qreal before = QLineF(anchor, r.center()).length();
        const qreal after = QLineF(anchor, pos).length();
        if (before < 1.0 || after < 1.0)
            return;

        const qreal factor = after / before;
        const int size = qBound(6, qRound(m_dragBaseFontSize * factor), 400);
        m_fontSize = size;
        if (m_dragBaseBoxWidth > 0)
            m_boxWidth = qMax<qreal>(20, m_dragBaseBoxWidth * factor);
        return;
    }
    }
}

void TextShape::scaleFontBy(qreal factor)
{
    m_fontSize = qBound(6, qRound(m_fontSize * factor), 400);
    if (m_boxWidth > 0)
        m_boxWidth = qMax<qreal>(20, m_boxWidth * factor);
}

void TextShape::scaleAbout(const QPointF &center, qreal factor)
{
    m_pos = center + (m_pos - center) * factor;
    m_fontSize = qBound(6, qRound(m_fontSize * factor), 400);
    if (m_boxWidth > 0)
        m_boxWidth *= factor;
}

QJsonObject TextShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("text");
    o[QStringLiteral("text")] = m_text;
    o[QStringLiteral("x")] = m_pos.x();
    o[QStringLiteral("y")] = m_pos.y();
    o[QStringLiteral("fontSize")] = m_fontSize;
    o[QStringLiteral("boxWidth")] = m_boxWidth;
    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("bgColor")] = m_bgColor.name(QColor::HexArgb);
    o[QStringLiteral("outline")] = m_outline;
    o[QStringLiteral("outlineColor")] = m_outlineColor.name(QColor::HexArgb);
    o[QStringLiteral("outlineWidth")] = m_outlineWidth;
    o[QStringLiteral("rotation")] = m_rotation;
    return o;
}

std::shared_ptr<Annotation> TextShape::clone() const
{
    auto copy = std::make_shared<TextShape>();
    copy->m_text = m_text;
    copy->m_pos = m_pos;
    copy->m_fontSize = m_fontSize;
    copy->m_boxWidth = m_boxWidth;
    copy->m_color = m_color;
    copy->m_bgColor = m_bgColor;
    copy->m_outline = m_outline;
    copy->m_outlineColor = m_outlineColor;
    copy->m_outlineWidth = m_outlineWidth;
    copy->m_rotation = m_rotation;
    return copy;
}
