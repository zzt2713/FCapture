#pragma once

#include "annotate/Annotation.h"

#include <QFont>
#include <QPainterPath>
#include <QVector>
#include <QString>

class QTextDocument;

// 文字标注
class TextShape : public Annotation
{
public:
    TextShape();

    // 一段局部样式（按字符区间）。借鉴 Snow Shot 的 TextAnnotationRun：
    // 文字整体样式仍由成员变量控制，这里只存"局部覆盖"。
    struct Run
    {
        int    start = 0;                      // 起始字符下标（含）
        int    end = 0;                        // 结束字符下标（不含）
        QColor highlight = Qt::transparent;    // 高亮背景（alpha>0 才生效）
        QColor color;                          // 前景色（无效则用整体色）
        bool   bold = false;
        bool   hasColor = false;
        bool   hasBold = false;
    };

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translateContent(const QPointF &delta) override;
    void scaleAbout(const QPointF &center, qreal factor) override;
    QVector<QPointF> rawHandles() const override;
    void moveHandleRaw(int index, const QPointF &pos) override;
    void beginHandleDrag() override;
    void endHandleDrag() override { m_hasDragBaseline = false; }
    bool isEditable() const override { return true; }
    QJsonObject toJson() const override;
    std::shared_ptr<Annotation> clone() const override;

    void setText(const QString &t);
    QString text() const { return m_text; }

    void setPosition(const QPointF &pos);
    QPointF position() const { return m_pos; }

    void setFontSize(int px);
    int fontSize() const { return m_fontSize; }

    // 拖动左右侧边手柄改宽度（自动换行）；0 = 按内容撑开不换行
    qreal boxWidth() const { return m_boxWidth; }

    // 文字背景填充
    void setBackgroundColor(const QColor &c) { m_bgColor = c; }
    QColor backgroundColor() const { return m_bgColor; }

    // 文字描边
    void setOutline(bool on) { m_outline = on; }
    bool outline() const { return m_outline; }
    void setOutlineColor(const QColor &c) { m_outlineColor = c; }
    QColor outlineColor() const { return m_outlineColor; }
    void setOutlineWidth(int w) { m_outlineWidth = qMax(1, w); }
    int outlineWidth() const { return m_outlineWidth; }

    QFont font() const;
    QRectF layoutRect() const;

    // ---- 局部样式 ----
    QVector<Run> runs() const { return m_runs; }
    void setRuns(const QVector<Run> &runs);
    // 给 [start, end) 区间叠加高亮背景（color 无效 = 清除该区间高亮）
    void applyHighlight(int start, int end, const QColor &color);
    // 给 [start, end) 区间叠加前景色
    void applyTextColor(int start, int end, const QColor &color);
    void clearLocalStyles();

    // 拖角等比缩放：直接按比例改字号（文本框高度自适应内容，所以只有字号有意义）
    void scaleFontBy(qreal factor);

private:
    // 构建与输入框一致的 QTextDocument
    QTextDocument *createDocument() const;
    // 逐字形取轮廓路径（坐标原点 = layoutRect 左上角），用于描边
    QPainterPath buildGlyphPath() const;

    QString m_text;
    QPointF m_pos;
    int     m_fontSize = 16;
    qreal   m_boxWidth = 0;

    // 拖拽缩放基准（beginHandleDrag 时快照）。
    // 没有它就只能"相对当前值再乘一次比例"，拖一下放大十倍、反向缩不回去。
    bool    m_hasDragBaseline = false;
    int     m_dragBaseFontSize = 16;
    qreal   m_dragBaseBoxWidth = 0;
    QRectF  m_dragBaseRect;
    QPointF m_dragAnchor;
    QVector<Run> m_runs;   // 局部样式区间（按 start 升序、互不重叠）
    QColor  m_bgColor = Qt::transparent;
    bool    m_outline = false;
    QColor  m_outlineColor = QColor(Qt::black);
    int     m_outlineWidth = 2;
};
