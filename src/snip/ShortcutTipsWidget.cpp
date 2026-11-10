#include "snip/ShortcutTipsWidget.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

namespace {
// 面板排版常量（snow ScreenshotShortcutHintsWidget 同款节奏：
// 内边距 16 / 行高 22 / 行距 16 / chip 水平内边距 16）
constexpr int kTipsPad = 16;
constexpr int kTipsBar = 3;      // 左侧主色条宽度
constexpr int kTipsGap = 16;
constexpr int kChipPadH = 16;
constexpr int kChipPadV = 2;
} // namespace

ShortcutTipsWidget::ShortcutTipsWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void ShortcutTipsWidget::setRows(const QVector<Row> &rows)
{
    if (m_rows.size() == rows.size()) {
        bool same = true;
        for (int i = 0; i < rows.size(); ++i) {
            if (m_rows.at(i).keys != rows.at(i).keys
                || m_rows.at(i).desc != rows.at(i).desc) {
                same = false;
                break;
            }
        }
        if (same)
            return;
    }
    m_rows = rows;
    relayout();
}

void ShortcutTipsWidget::relayout()
{
    m_metrics = computeMetrics();
    const QSize size(kTipsBar + kTipsPad + m_metrics.labelColumn + m_metrics.colonWidth
                         + m_metrics.chipColumn + kTipsPad,
                     kTipsPad * 2 + m_metrics.rowHeight * m_rows.size()
                         + kTipsGap * qMax(0, m_rows.size() - 1));
    setFixedSize(size);
    update();
}

ShortcutTipsWidget::Metrics ShortcutTipsWidget::computeMetrics() const
{
    QFont font = Theme::appFont();
    font.setPointSize(9);
    const QFontMetrics fm(font);

    Metrics m;
    m.chipHeight = fm.height() + kChipPadV * 2;
    m.rowHeight = qMax(22, m.chipHeight);
    m.colonWidth = fm.horizontalAdvance(QLatin1Char(':')) + 2 + 8;
    for (const Row &r : m_rows) {
        m.labelColumn = qMax(m.labelColumn, fm.horizontalAdvance(r.desc));
        m.chipColumn = qMax(m.chipColumn,
                            fm.horizontalAdvance(r.keys) + kChipPadH * 2);
    }
    return m;
}

void ShortcutTipsWidget::setObscured(bool obscured)
{
    if (m_obscured == obscured)
        return;
    m_obscured = obscured;
    // 被遮挡时整体隐藏（snow 同款：不做部分裁剪）。
    // 子控件的 show/hide 由 Qt 自动让父窗口对应区域失效重绘，无残影。
    if (obscured || m_rows.isEmpty()) {
        hide();
    } else {
        show();
        raise();
    }
}

void ShortcutTipsWidget::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    if (m_rows.isEmpty())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font = Theme::appFont();
    font.setPointSize(9);
    p.setFont(font);

    const QRect box = rect();

    // 面板：snow 同款黑 163 底 + 圆角 6
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 163));
    p.drawRoundedRect(box.adjusted(0, 0, -1, -1), 6, 6);

    // 左侧 3px 主色条（裁进圆角，不越出面板轮廓）
    p.save();
    p.setClipRect(0, 0, kTipsBar, height());
    p.setBrush(QColor(0x3D, 0x8B, 0xFD));
    p.drawRoundedRect(box.adjusted(0, 0, -1, -1), 6, 6);
    p.restore();

    int top = kTipsPad;
    for (const Row &r : m_rows) {
        const int labelX = kTipsBar + kTipsPad;

        // 说明在前（snow 版式），次要白
        p.setPen(QColor(255, 255, 255, 184));
        p.drawText(QRect(labelX, top, m_metrics.labelColumn, m_metrics.rowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, r.desc);

        // 冒号
        int x = labelX + m_metrics.labelColumn + 2;
        p.setPen(QColor(255, 255, 255, 128));
        p.drawText(QRect(x, top, m_metrics.colonWidth, m_metrics.rowHeight),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral(":"));
        x += m_metrics.colonWidth;

        // 主色描边 chip
        const QRect chipRect(x, top + (m_metrics.rowHeight - m_metrics.chipHeight) / 2,
                             m_metrics.chipColumn, m_metrics.chipHeight);
        p.setPen(QPen(QColor(0x3D, 0x8B, 0xFD), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chipRect, 6, 6);
        p.setPen(QColor(0x9D, 0xC1, 0xFF));
        p.drawText(chipRect, Qt::AlignCenter, r.keys);

        top += m_metrics.rowHeight + kTipsGap;
    }
}
