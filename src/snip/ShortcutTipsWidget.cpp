#include "snip/ShortcutTipsWidget.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

namespace {
// 严格对齐 snow 的 ScreenshotShortcutHintsWidget 常量：
//   Margin 16 / Padding 16 / Radius 6 / FontSize 14 / LineHeight 22
//   RowSpacing 16 / ColonMarginLeft 2 / ColonMarginRight 8
//   ChipPaddingH 16 / ChipPaddingV 2 / ChipRadius 6
constexpr int kTipsPad = 16;
constexpr int kTipsGap = 16;
constexpr int kChipPadH = 16;
constexpr int kChipPadV = 2;
constexpr int kChipRadius = 6;
constexpr int kPanelRadius = 6;
constexpr int kColonMarginLeft = 2;
constexpr int kColonMarginRight = 8;
constexpr int kHintFontPx = 14;
constexpr int kHintLineHeight = 22;
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
    const QSize size(kTipsPad * 2 + m_metrics.labelColumn + m_metrics.colonWidth
                         + m_metrics.chipColumn,
                     kTipsPad * 2 + kHintLineHeight * m_rows.size()
                         + kTipsGap * qMax(0, m_rows.size() - 1));
    setFixedSize(size);
    update();
}

ShortcutTipsWidget::Metrics ShortcutTipsWidget::computeMetrics() const
{
    QFont font = Theme::appFont();
    font.setPixelSize(kHintFontPx);
    const QFontMetrics fm(font);

    Metrics m;
    m.chipHeight = kHintLineHeight + kChipPadV * 2;
    m.rowHeight = kHintLineHeight;
    m.colonWidth = fm.horizontalAdvance(QLatin1Char(':')) + kColonMarginLeft
                   + kColonMarginRight;
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
    font.setPixelSize(kHintFontPx);
    p.setFont(font);

    // 面板：黑 163 底 + 圆角 6（snow 同款，无左侧色条）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 163));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                      kPanelRadius, kPanelRadius);

    const QFontMetrics fm(font);
    const QColor contentColor(Qt::white);
    int rowTop = kTipsPad;

    for (const Row &r : m_rows) {
        // 说明文字：白 184
        const int labelWidth = fm.horizontalAdvance(r.desc);
        p.setPen(QColor(255, 255, 255, 184));
        p.drawText(QRect(kTipsPad, rowTop, labelWidth, kHintLineHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, r.desc);

        // 冒号
        const int colonLeft = kTipsPad + labelWidth + kColonMarginLeft;
        const int colonWidth = fm.horizontalAdvance(QLatin1Char(':'));
        p.setPen(QColor(255, 255, 255, 184));
        p.drawText(QRect(colonLeft, rowTop, colonWidth, kHintLineHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(":"));

        // 按键 chip：**白色** 1px 描边 + 白色文字（snow 用 contentColor，不是主色）
        const int chipLeft = colonLeft + colonWidth + kColonMarginRight;
        const QRectF chipRect(chipLeft, rowTop - kChipPadV,
                              m_metrics.chipColumn, kHintLineHeight + kChipPadV * 2);
        p.setPen(QPen(contentColor, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(chipRect.adjusted(0.5, 0.5, -0.5, -0.5),
                          kChipRadius, kChipRadius);

        p.setPen(contentColor);
        p.drawText(chipRect, Qt::AlignCenter, r.keys);

        rowTop += kHintLineHeight + kTipsGap;
    }
}
