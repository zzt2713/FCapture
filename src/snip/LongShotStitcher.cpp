#include "snip/LongShotStitcher.h"

#include <QtGlobal>

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

bool LongShotStitcher::matchOffset(const QImage &frame, int *offset) const
{
    const int w = frame.width();
    const int h = frame.height();
    if (w != m_lastFrame.width() || h != m_lastFrame.height())
        return false;

    // 重叠不足 1/3 就不可信（重复图案、长条纹容易误匹配）；
    // 小视口（高度不足 128）时放宽到一半高度，否则偏移搜索范围为 0。
    const int minOverlap = qMin(qMax(64, h / 3), h / 2);
    const int maxShift = qMax(0, h - minOverlap);

    // ---- 第一轮：粗搜索（大步距扫全程）----
    int coarseBest = 0;
    qint64 coarseScore = LLONG_MAX;
    const int coarseStepX = qMax(8, w / 48);
    for (int s = -maxShift; s <= maxShift; ++s) {
        const qint64 score = sadAt(frame, m_lastFrame, s, coarseStepX, kCoarseStepY);
        if (score < coarseScore) {
            coarseScore = score;
            coarseBest = s;
        }
    }
    if (coarseScore == LLONG_MAX)
        return false;

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
        return false;
    // 歧义检测：次优和最优太接近 = 重复图案（表格/列表），不可靠
    if (secondScore != LLONG_MAX && bestScore * 100 > secondScore * 85)
        return false;

    *offset = bestShift;
    return true;
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
        return true;
    }

    int shift = 0;
    if (!matchOffset(frame, &shift)) {
        // 连续多帧都和上一帧对不上（滚动过快 / 暂停期间跳了太远）→
        // 重新锚定到当前帧，否则后续帧永远和过期的上一帧比，拼接永久卡死
        if (++m_missStreak >= 3) {
            m_lastFrame = frame;
            m_missStreak = 0;
        }
        return false;
    }
    m_missStreak = 0;
    if (shift == 0)
        return false;

    const int w = frame.width();
    const int h = frame.height();
    const int grow = qAbs(shift);
    if (m_canvas.height() + grow > kMaxCanvasHeight) {
        m_full = true;
        return false;
    }

    // 画布高度动态扩展：新内容接到顶端或底端，整段像素平移拷贝
    QImage grown(w, m_canvas.height() + grow, QImage::Format_RGB32);
    const size_t lineBytes = static_cast<size_t>(w) * 4;
    const int oldH = m_canvas.height();
    if (shift > 0) {
        // 向下滚：旧画布 + 新帧底部 grow 行
        for (int y = 0; y < oldH; ++y)
            std::memcpy(grown.scanLine(y), m_canvas.constScanLine(y), lineBytes);
        for (int y = 0; y < grow; ++y)
            std::memcpy(grown.scanLine(oldH + y),
                        frame.constScanLine(h - grow + y), lineBytes);
    } else {
        // 向上滚：新帧顶部 grow 行 + 旧画布
        for (int y = 0; y < grow; ++y)
            std::memcpy(grown.scanLine(y), frame.constScanLine(y), lineBytes);
        for (int y = 0; y < oldH; ++y)
            std::memcpy(grown.scanLine(grow + y), m_canvas.constScanLine(y), lineBytes);
    }

    m_canvas = grown;
    m_lastFrame = frame;
    ++m_accepted;
    return true;
}
