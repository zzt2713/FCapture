#include "annotate/shapes/MagnifyShape.h"

#include <QJsonArray>
#include <QJsonObject>

MagnifyShape::MagnifyShape()
{
    m_color = QColor(0xED, 0x1C, 0x24);
    m_penWidth = 2;
}

Annotation::Type MagnifyShape::type() const
{
    return Type::Magnify;
}

void MagnifyShape::setSourceRect(const QRectF &rect, const QImage &snapshot, qreal dpr,
                                 const QPointF &origin)
{
    // 记住整张快照：之后用户拖动源框时要重新裁片
    m_snapshot = snapshot;
    m_snapshotDpr = dpr > 0 ? dpr : 1.0;

    m_sourceRect = rect.normalized();
    m_origin = origin;
    recaptureSource(m_sourceRect);
}

void MagnifyShape::setSourceRectPreview(const QRectF &rect)
{
    m_sourceRect = rect.normalized();
}

void MagnifyShape::captureSource()
{
    recaptureSource(m_sourceRect);
}

void MagnifyShape::recaptureSource(const QRectF &newRect)
{
    m_sourceRect = newRect.normalized();

    if (m_snapshot.isNull() || m_sourceRect.isEmpty()) {
        m_sourcePatch = QImage();
        return;
    }

    const QRect phys(qRound((m_sourceRect.x() - m_origin.x()) * m_snapshotDpr),
                     qRound((m_sourceRect.y() - m_origin.y()) * m_snapshotDpr),
                     qRound(m_sourceRect.width() * m_snapshotDpr),
                     qRound(m_sourceRect.height() * m_snapshotDpr));
    if (phys.isEmpty()) {
        m_sourcePatch = QImage();
        return;
    }

    m_sourcePatch = m_snapshot.copy(phys.intersected(m_snapshot.rect()));
    m_sourcePatch.setDevicePixelRatio(1.0);
}

qreal MagnifyShape::factor() const
{
    if (m_sourceRect.isEmpty() || m_targetRect.isEmpty())
        return m_factor;
    return m_targetRect.width() / qMax<qreal>(1.0, m_sourceRect.width());
}

QRectF MagnifyShape::rawBoundingRect() const
{
    QRectF r = m_targetRect;
    if (!m_sourceRect.isEmpty())
        r = r.united(m_sourceRect);
    return r.adjusted(-2, -2, 2, 2);
}

void MagnifyShape::paintContent(QPainter *p) const
{
    if (hasSource() && hasTarget()) {
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform, false);
        p->drawImage(m_targetRect, m_sourcePatch,
                     QRectF(0, 0, m_sourcePatch.width(), m_sourcePatch.height()));
        p->restore();
    }

    // 画完之后只保留放大后的画面：
    // 目标框 / 源框 / 引导线只是"正在摆放放大镜"时的辅助线，
    // 一旦落定就变成纯图像内容，否则成品截图里永远挂着一个红框。
    if (m_finished)
        return;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    // 目标框
    if (m_showBorder && !m_targetRect.isEmpty()) {
        p->setPen(QPen(m_color, qMax(1, m_penWidth), Qt::SolidLine));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(m_targetRect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }

    // 源框 + 引导线
    if (!m_sourceRect.isEmpty()) {
        QPen dash(m_color, 1, Qt::DashLine);
        dash.setDashPattern({4, 3});
        p->setPen(dash);
        p->setBrush(Qt::NoBrush);
        p->drawRect(m_sourceRect.adjusted(0.5, 0.5, -0.5, -0.5));

        if (!m_targetRect.isEmpty()) {
            // 源框四角指向目标框四角，取最近的一对角连线
            const QPointF candidates[2][2] = {
                { m_sourceRect.topLeft(), m_targetRect.topLeft() },
                { m_sourceRect.bottomRight(), m_targetRect.bottomRight() }
            };
            for (const auto &pair : candidates) {
                if (pair[0] == pair[1])
                    continue;
                p->drawLine(pair[0], pair[1]);
            }
        }
    }

    p->restore();
}

bool MagnifyShape::hitTestContent(const QPointF &pt, qreal tolerance) const
{
    const QRectF t = m_targetRect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (!t.isEmpty() && t.contains(pt))
        return true;
    const QRectF s = m_sourceRect.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    return !s.isEmpty() && s.contains(pt);
}

void MagnifyShape::translateContent(const QPointF &delta)
{
    m_targetRect.translate(delta);

    // 源框跟着移动时指向的是底图的另一块区域，必须重新裁片，
    // 否则画面内容和虚线框对不上。
    if (!m_sourceRect.isEmpty())
        recaptureSource(m_sourceRect.translated(delta));
}

void MagnifyShape::scaleAbout(const QPointF &center, qreal factor)
{
    const QPointF tl = center + (m_targetRect.topLeft() - center) * factor;
    const QPointF br = center + (m_targetRect.bottomRight() - center) * factor;
    m_targetRect = QRectF(tl, br).normalized();
    m_factor = MagnifyShape::factor();
}

QVector<QPointF> MagnifyShape::rawHandles() const
{
    QVector<QPointF> handles;

    // 0~7：目标框 8 个锚点
    if (!m_targetRect.isEmpty()) {
        const QRectF r = m_targetRect;
        handles << r.topLeft() << QPointF(r.center().x(), r.top()) << r.topRight()
                << QPointF(r.right(), r.center().y()) << r.bottomRight()
                << QPointF(r.center().x(), r.bottom()) << r.bottomLeft()
                << QPointF(r.left(), r.center().y());
    }

    // 8~11：源框 4 个角（可拖动，重新裁片）
    if (!m_sourceRect.isEmpty()) {
        const QRectF s = m_sourceRect;
        handles << s.topLeft() << s.topRight() << s.bottomRight() << s.bottomLeft();
    }

    return handles;
}

void MagnifyShape::moveHandleRaw(int index, const QPointF &pos)
{
    // 源框 4 角：拖动后重新从快照裁片
    if (index >= kSourceHandleStart) {
        if (m_sourceRect.isEmpty())
            return;

        QRectF s = m_sourceRect;
        const qreal sl = s.left();
        const qreal sr = s.right();
        const qreal st = s.top();
        const qreal sb = s.bottom();
        switch (index - kSourceHandleStart) {
        case 0: s = QRectF(pos, QPointF(sr, sb)).normalized(); break;
        case 1: s = QRectF(QPointF(sl, pos.y()), QPointF(pos.x(), sb)).normalized(); break;
        case 2: s = QRectF(QPointF(sl, st), pos).normalized(); break;
        case 3: s = QRectF(QPointF(pos.x(), st), QPointF(sr, sb)).normalized(); break;
        default: return;
        }
        if (s.width() >= 4 && s.height() >= 4)
            recaptureSource(s);
        return;
    }

    if (index < 0 || index >= kTargetHandleCount || m_targetRect.isEmpty())
        return;

    // 对边固定重建矩形（拖过对边时锚点翻转，不能让它往回跑）
    QRectF r = m_targetRect;
    const qreal left = r.left();
    const qreal right = r.right();
    const qreal top = r.top();
    const qreal bottom = r.bottom();
    switch (index) {
    case 0: r = QRectF(pos, QPointF(right, bottom)).normalized(); break;
    case 1: r = QRectF(QPointF(left, pos.y()), QPointF(right, bottom)).normalized(); break;
    case 2: r = QRectF(QPointF(left, pos.y()), QPointF(pos.x(), bottom)).normalized(); break;
    case 3: r = QRectF(QPointF(left, top), QPointF(pos.x(), bottom)).normalized(); break;
    case 4: r = QRectF(QPointF(left, top), pos).normalized(); break;
    case 5: r = QRectF(QPointF(left, top), QPointF(right, pos.y())).normalized(); break;
    case 6: r = QRectF(QPointF(pos.x(), top), QPointF(right, bottom)).normalized(); break;
    case 7: r = QRectF(QPointF(pos.x(), top), QPointF(right, bottom)).normalized(); break;
    default: return;
    }
    // 同上：不过滤过小的中间态，否则拖过对边时会卡在退化矩形
    m_targetRect = r;
    m_factor = MagnifyShape::factor();
}

QJsonObject MagnifyShape::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = QStringLiteral("magnify");
    o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    o[QStringLiteral("penWidth")] = m_penWidth;
    o[QStringLiteral("factor")] = m_factor;
    o[QStringLiteral("showBorder")] = m_showBorder;
    o[QStringLiteral("source")] = QJsonArray{m_sourceRect.x(), m_sourceRect.y(),
                                             m_sourceRect.width(), m_sourceRect.height()};
    o[QStringLiteral("target")] = QJsonArray{m_targetRect.x(), m_targetRect.y(),
                                             m_targetRect.width(), m_targetRect.height()};
    return o;
}

std::shared_ptr<Annotation> MagnifyShape::clone() const
{
    auto copy = std::make_shared<MagnifyShape>();
    copy->m_sourceRect = m_sourceRect;
    copy->m_targetRect = m_targetRect;
    copy->m_sourcePatch = m_sourcePatch;   // 隐式共享，拷贝代价低
    copy->m_snapshot = m_snapshot;         // 拖动克隆出的副本也要能重新裁片
    copy->m_snapshotDpr = m_snapshotDpr;
    copy->m_origin = m_origin;
    copy->m_factor = m_factor;
    copy->m_showBorder = m_showBorder;
    copy->m_color = m_color;
    copy->m_penWidth = m_penWidth;
    return copy;
}
