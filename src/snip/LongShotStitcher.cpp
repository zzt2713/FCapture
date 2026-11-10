#include "snip/LongShotStitcher.h"

#include <QtGlobal>

#include <algorithm>

#include <cstring>
#include <climits>

namespace {

// 一帧里相邻采样行/列的步距：先粗后细两轮搜索用
constexpr int kCoarseStepY = 8;
constexpr int kRefineStepY = 2;
constexpr int kRefineWindow = 8;

} // namespace

void LongShotStitcher::reset()
{
    m_canvas = QImage();
    m_lastFrame = QImage();
    m_accepted = 0;
    m_missStreak = 0;
    m_full = false;
}

qint64 LongShotStitcher::sadAt(const QImage &frame, const QImage &prev, int shift,
                               int stepX, int stepY)
{
    const int w = frame.width();
    const int h = frame.height();

    // shift>0：frame 的第 y 行 ≈ prev 的第 y+shift 行（内容向下滚了 shift）
    const int yBegin = qMax(0, -shift);
    const int yEnd = qMin(h, h - shift);
    if (yEnd - yBegin < 2)
        return LLONG_MAX;

    qint64 sum = 0;
    qint64 samples = 0;
    for (int y = yBegin; y < yEnd; y += stepY) {
        const auto *a = reinterpret_cast<const QRgb *>(frame.constScanLine(y));
        const auto *b = reinterpret_cast<const QRgb *>(prev.constScanLine(y + shift));
        for (int x = 0; x < w; x += stepX) {
            const QRgb pa = a[x];
            const QRgb pb = b[x];
            sum += qAbs(qRed(pa) - qRed(pb))
                   + qAbs(qGreen(pa) - qGreen(pb))
                   + qAbs(qBlue(pa) - qBlue(pb));
            ++samples;
        }
    }
    return samples ? sum / samples : LLONG_MAX;
}

// ---------------------------------------------------------------- 列投影匹配
//
// 借鉴 snow-stitch-images 的降维思路：把二维图像压成"每行平均亮度"的一维序列，
// 先在一维上找候选偏移，再用二维 SAD 验证。
// 好处：
//   1. 一维搜索比二维快一个数量级（不需要逐像素比）
//   2. 对页面里的固定元素（悬浮按钮、导航条）不敏感——它们只影响很少的列，
//      平均亮度里权重很小，不会把整体偏移带偏
//   3. 对横向 1~2px 漂移免疫（列投影本来就抹掉了横向信息）

QVector<qint64> LongShotStitcher::rowProjection(const QImage &img, int *count)
{
    QVector<qint64> out;
    if (img.isNull())
        return out;

    const int w = img.width();
    const int h = img.height();
    out.reserve(h);

    // 横向采样步长：宽图不必逐列平均
    const int stepX = qMax(1, w / 64);
    int n = 0;
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        qint64 sum = 0;
        for (int x = 0; x < w; x += stepX) {
            const QRgb px = line[x];
            // 亮度近似：不用浮点，绿色权重最高
            sum += (qRed(px) * 2 + qGreen(px) * 3 + qBlue(px)) / 6;
        }
        out.append(sum);
        ++n;
    }
    if (count)
        *count = n;
    return out;
}

QVector<int> LongShotStitcher::projectionCandidates(const QVector<qint64> &cur,
                                                    const QVector<qint64> &prev,
                                                    int maxShift, int topK) const
{
    QVector<QPair<qint64, int>> scored;
    const int h = cur.size();
    if (h <= 0 || prev.size() != h)
        return {};

    const int minOverlap = qMin(qMax(64, h / 3), h / 2);

    for (int s = -maxShift; s <= maxShift; ++s) {
        const int yBegin = qMax(0, -s);
        const int yEnd = qMin(h, h - s);
        if (yEnd - yBegin < minOverlap)
            continue;

        qint64 sum = 0;
        int cnt = 0;
        for (int y = yBegin; y < yEnd; y += 2) {
            sum += qAbs(cur.at(y) - prev.at(y + s));
            ++cnt;
        }
        if (cnt > 0)
            scored.append({sum / cnt, s});
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<qint64, int> &a, const QPair<qint64, int> &b) {
                  return a.first < b.first;
              });

    QVector<int> out;
    for (int i = 0; i < scored.size() && i < topK; ++i)
        out.append(scored.at(i).second);
    return out;
}

LongShotStitcher::Match LongShotStitcher::matchOffset(const QImage &frame) const
{
    Match result;
    const int w = frame.width();
    const int h = frame.height();
    if (w != m_lastFrame.width() || h != m_lastFrame.height())
        return result;

    // 重叠不足 1/3 就不可信（重复图案、长条纹容易误匹配）；
    // 小视口（高度不足 128）时放宽到一半高度，否则偏移搜索范围为 0。
    const int minOverlap = qMin(qMax(64, h / 3), h / 2);
    const int maxShift = qMax(0, h - minOverlap);

    // ---- 第一轮：行投影粗搜（一维，快且抗固定元素干扰）----
    const QVector<qint64> projCur = rowProjection(frame);
    const QVector<qint64> projPrev = rowProjection(m_lastFrame);
    const QVector<int> candidates = projectionCandidates(projCur, projPrev, maxShift, 3);

    if (candidates.isEmpty())
        return result;

    // 对前 3 个候选做二维 SAD 验证，取最优（避免一维匹配的歧义）
    int coarseBest = candidates.first();
    qint64 coarseScore = LLONG_MAX;
    const int coarseStepX = qMax(8, w / 48);
    for (int cand : candidates) {
        const qint64 score = sadAt(frame, m_lastFrame, cand, coarseStepX, kCoarseStepY);
        if (score < coarseScore) {
            coarseScore = score;
            coarseBest = cand;
        }
    }
    if (coarseScore == LLONG_MAX)
        return result;

    // ---- 第二轮：细搜索（粗结果 ±8px，小步距）----
    qint64 bestScore = LLONG_MAX;
    qint64 secondScore = LLONG_MAX;
    int bestShift = coarseBest;
    const int fineStepX = qMax(4, w / 96);
    const int lo = qMax(-maxShift, coarseBest - kRefineWindow);
    const int hi = qMin(maxShift, coarseBest + kRefineWindow);
    for (int s = lo; s <= hi; ++s) {
        const qint64 score = sadAt(frame, m_lastFrame, s, fineStepX, kRefineStepY);
        if (score < bestScore) {
            secondScore = bestScore;
            bestScore = score;
            bestShift = s;
        } else if (score < secondScore) {
            secondScore = score;
        }
    }

    // 绝对阈值：平均差太大 = 前后帧不是同一块内容
    if (bestScore > kMaxSampleSad)
        return result;
    // 歧义检测：次优和最优太接近 = 重复图案（表格/列表），不可靠
    if (secondScore != LLONG_MAX && bestScore * 100 > secondScore * 85)
        return result;

    // 置信度：把"每采样点 RGB 三通道绝对差和"归一化成 0~1。
    // 3 通道最大差 = 255*3 = 765，所以 1 - bestScore/765。
    // 这个值比单纯的 bestScore 直观，也便于设统一阈值。
    result.ok = true;
    result.offset = bestShift;
    result.confidence = 1.0 - qMin<qreal>(1.0, qreal(bestScore) / 765.0);
    return result;
}

bool LongShotStitcher::feed(const QImage &input)
{
    if (m_full || input.isNull())
        return false;

    const QImage frame = input.convertToFormat(QImage::Format_RGB32);
    if (frame.isNull())
        return false;

    // 第一帧直接打底
    if (m_canvas.isNull()) {
        m_canvas = frame;
        m_lastFrame = frame;
        ++m_accepted;
        m_lastConfidence = 1.0;
        m_lastGrewTop = false;
        return true;
    }

    const Match m = matchOffset(frame);
    m_lastConfidence = m.confidence;

    // 置信度不够：这一帧大概率是滚动动画中途 / 动态内容 / 页面切换，
    // 硬拼进去就是接缝重影。宁可丢帧也不拼错。
    if (!m.ok || m.confidence < kMinConfidence) {
        // 连续多帧都对不上（滚动过快 / 暂停期间跳太远）→ 重新锚定，
        // 否则后续帧永远和过期的上一帧比，拼接永久卡死
        if (++m_missStreak >= 3) {
            m_lastFrame = frame;
            m_missStreak = 0;
        }
        return false;
    }
    m_missStreak = 0;

    const int shift = m.offset;
    if (shift == 0)
        return false;

    const int w = frame.width();
    const int h = frame.height();
    const int grow = qAbs(shift);
    if (m_canvas.height() + grow > kMaxCanvasHeight) {
        m_full = true;
        return false;
    }

    // 画布高度动态扩展。借鉴 snow-stitch-images 的 append_and_repaint：
    // 不是简单"追加新行"，而是在重叠带上用新帧重新绘制一段——
    // 这样接缝处用的是最后一次抓到的（通常最清晰的）像素。
    QImage grown(w, m_canvas.height() + grow, QImage::Format_RGB32);
    const size_t lineBytes = static_cast<size_t>(w) * 4;
    const int oldH = m_canvas.height();

    // 边缘剔除量不能超过增长量本身，否则会取到重复内容
    const int trim = qMin(kEdgeTrimRows, qMax(0, grow / 2));

    if (shift > 0) {
        // 向下滚：旧画布 + 新帧底部 grow 行（跳过最底下 trim 行）
        for (int y = 0; y < oldH; ++y)
            std::memcpy(grown.scanLine(y), m_canvas.constScanLine(y), lineBytes);
        for (int y = 0; y < grow; ++y) {
            const int srcY = h - grow + y - trim;
            const int safeY = qBound(0, srcY, h - 1);
            std::memcpy(grown.scanLine(oldH + y), frame.constScanLine(safeY), lineBytes);
        }

        // 重叠区择优：接缝上方 kRepaintRows 行，若新帧的对应像素更"锐利"
        // （局部梯度更大 = 更清晰，通常意味着不是滚动动画中途的模糊帧），
        // 就用新帧覆盖旧画布。避免把模糊的旧行留在接缝处。
        const int repaint = qMin(kRepaintRows, oldH);
        for (int y = 0; y < repaint; ++y) {
            const int dstY = oldH - repaint + y;
            const int srcY = h - grow - repaint + y;   // 新帧中对应的行
            if (srcY < 0 || srcY >= h)
                continue;
            const auto *newLine = reinterpret_cast<const QRgb *>(frame.constScanLine(srcY));
            auto *dstLine = reinterpret_cast<QRgb *>(grown.scanLine(dstY));
            const auto *oldLine = reinterpret_cast<const QRgb *>(m_canvas.constScanLine(dstY));
            for (int x = 0; x < w; ++x) {
                // 用相邻像素差近似"锐度"：差得越大说明这一行细节越多（越不像模糊）
                const QRgb nc = newLine[x];
                const QRgb oc = oldLine[x];
                const int diffNew = qAbs(qRed(nc) - qRed(oldLine[qMax(0, x - 1)]));
                const int diffOld = qAbs(qRed(oc) - qRed(oldLine[qMax(0, x - 1)]));
                if (diffNew > diffOld)
                    dstLine[x] = nc;
            }
        }
    } else {
        // 向上滚：新帧顶部 grow 行（跳过最上面 trim 行）+ 旧画布
        for (int y = 0; y < grow; ++y) {
            const int srcY = y + trim;
            const int safeY = qBound(0, srcY, h - 1);
            std::memcpy(grown.scanLine(y), frame.constScanLine(safeY), lineBytes);
        }
        for (int y = 0; y < oldH; ++y)
            std::memcpy(grown.scanLine(grow + y), m_canvas.constScanLine(y), lineBytes);

        // 重叠区择优（向上滚版本）
        const int repaint = qMin(kRepaintRows, oldH);
        for (int y = 0; y < repaint; ++y) {
            const int dstY = grow + y;
            const int srcY = grow - trim + y;
            if (srcY < 0 || srcY >= h)
                continue;
            const auto *newLine = reinterpret_cast<const QRgb *>(frame.constScanLine(srcY));
            auto *dstLine = reinterpret_cast<QRgb *>(grown.scanLine(dstY));
            const auto *oldLine = reinterpret_cast<const QRgb *>(m_canvas.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb nc = newLine[x];
                const QRgb oc = oldLine[x];
                const int diffNew = qAbs(qRed(nc) - qRed(oldLine[qMax(0, x - 1)]));
                const int diffOld = qAbs(qRed(oc) - qRed(oldLine[qMax(0, x - 1)]));
                if (diffNew > diffOld)
                    dstLine[x] = nc;
            }
        }
    }

    m_canvas = grown;
    m_lastFrame = frame;
    m_lastGrewTop = shift < 0;
    ++m_accepted;
    return true;
}

