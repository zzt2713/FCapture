#pragma once

#include "annotate/Annotation.h"

#include <QFont>
#include <QPainterPath>
#include <QString>

class QTextDocument;

// 文字标注
class TextShape : public Annotation
{
public:
    TextShape();

    Type type() const override;
    QRectF rawBoundingRect() const override;
    void paintContent(QPainter *p) const override;
    bool hitTestContent(const QPointF &pt, qreal tolerance) const override;
    void translate(const QPointF &delta) override;
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
    QColor  m_bgColor = Qt::transparent;
    bool    m_outline = false;
    QColor  m_outlineColor = QColor(Qt::black);
    int     m_outlineWidth = 2;
};
