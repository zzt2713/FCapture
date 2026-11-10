#pragma once

#include <QImage>
#include <QVector>

// 滚动长截图拼接器。
//
// 视口尺寸固定：每帧喂入同一块屏幕区域的截图，用模板匹配算出
// 相对上一帧的滚动偏移（向下滚为正、向上滚为负），把新增内容接到
// 画布底端/顶端。两帧没有重叠（滚动过快）或匹配不可靠时直接丢弃
// 该帧——错位一帧整张长图就废了，宁缺毋滥。
class LongShotStitcher
{
public:
    // 一帧的匹配结果
    struct Match
    {
        bool   ok = false;      // 是否可靠
        int    offset = 0;      // 相对上一帧的滚动像素（向下为正）
        qreal  confidence = 0.0; // 归一化置信度 0~1（1 = 完全相同）
    };

    // 喂入一帧（物理像素尺寸须与之前一致）。画布有增长返回 true；
    // 没滚动 / 匹配不可靠 / 已达最大高度返回 false（可用 isFull 区分）。
    bool feed(const QImage &frame);

    void reset();

    bool isEmpty() const { return m_canvas.isNull(); }
    bool isFull() const { return m_full; }
    QImage canvas() const { return m_canvas; }
    int height() const { return m_canvas.height(); }
    int acceptedFrames() const { return m_accepted; }
    // 最近一次匹配的置信度（0~1），用于 UI 提示匹配质量
    qreal lastConfidence() const { return m_lastConfidence; }
    // 最近一次画布增长发生在顶端（true = 向上滚时前插到头部），
    // 预览栏据此高亮"最近捕获段"
    bool lastGrewTop() const { return m_lastGrewTop; }

private:
    // 在给定偏移范围内搜索最佳匹配，返回置信度最高的偏移。
    // 置信度 = 1 - 归一化平均绝对差，越接近 1 越可信。
    Match matchOffset(const QImage &frame) const;
    // 候选偏移的平均绝对差（每采样点 RGB 三通道差值和）
    static qint64 sadAt(const QImage &frame, const QImage &prev, int shift,
                        int stepX, int stepY);

    // 行投影：每行的平均亮度（用于粗筛偏移）。
    // 借鉴 snow-stitch-images 的"先降维再匹配"思路：把二维图像压成一维
    // 行亮度序列，对"页面有固定元素/横向漂移"比纯 SAD 更鲁棒，
    // 而且一维搜索相比二维快一个数量级。
    static QVector<qint64> rowProjection(const QImage &img, int *count = nullptr);
    // 用行投影做粗搜，返回候选偏移列表（按得分升序）
    QVector<int> projectionCandidates(const QVector<qint64> &cur,
                                      const QVector<qint64> &prev,
                                      int maxShift, int topK) const;

    QImage m_canvas;
    QImage m_lastFrame;
    int m_accepted = 0;
    int m_missStreak = 0;   // 连续匹配失败的帧数（滚动过快防卡死）
    bool m_full = false;
    qreal m_lastConfidence = 0.0;
    bool m_lastGrewTop = false;

    // 防止无限滚动把内存吃光：20000px 高的 4K 宽图约 300MB，够拼 20 屏
    static constexpr int kMaxCanvasHeight = 20000;
    // 每采样点 RGB 绝对差和的上限（3 通道各 10 级灰阶内视为同一内容）
    static constexpr qint64 kMaxSampleSad = 30;
    // 置信度下限：低于此值视为"匹配不可靠"，丢弃该帧。
    // 滚动动画中途、页面动态内容、亚像素位移都会让置信度掉下来。
    static constexpr qreal kMinConfidence = 0.90;
    // 拼接时丢弃新帧首尾的像素行数：滚动动画未稳定时这些行容易是
    // 模糊/撕裂的半帧内容，拼进去就是接缝重影。
    static constexpr int kEdgeTrimRows = 3;
    // 重叠区"择优重绘"的行数：接缝附近这些行用新帧覆盖旧画布，
    // 前提是新帧像素更锐利（避免留下滚动动画中途的模糊行）
    static constexpr int kRepaintRows = 8;
};
