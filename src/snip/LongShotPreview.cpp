#include "snip/LongShotPreview.h"

#include <QFont>
#include <QPainter>
#include <QPaintEvent>

LongShotPreview::LongShotPreview(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
}

void LongShotPreview::showRegion(const QImage &canvas, const QRect &canvasRect,
                                 const QRect &globalTarget)
{
    if (canvas.isNull() || canvasRect.isEmpty() || globalTarget.isEmpty()) {
        hide();
        return;
    }
    m_region = canvas.copy(canvasRect.intersected(canvas.rect()));
    if (m_region.isNull()) {
        hide();
        return;
    }
    setGeometry(globalTarget);
    show();
    raise();
    update();
}

void LongShotPreview::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 长图区段缩放铺满（= 选区天窗里看到的内容）
    if (!m_region.isNull())
        p.drawImage(rect(), m_region);
    else
        p.fillRect(rect(), Qt::black);

    // 「预览中」状态胶囊（Snow Shot 的 CanvasStatusReadout 同款：
    // 黑 150 底 / 圆角 4 / 内边距 3×6）
    p.setRenderHint(QPainter::Antialiasing, true);
    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);
    const QString text = QStringLiteral("预览中");
    QRect chip = p.fontMetrics().boundingRect(text).adjusted(-6, -3, 6, 4);
    chip.moveTopLeft(QPoint(8, 8));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(chip, 4, 4);
    p.setPen(Qt::white);
    p.drawText(chip, Qt::AlignCenter, text);
}
