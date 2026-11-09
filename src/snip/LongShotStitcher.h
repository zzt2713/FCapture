#pragma once

#include <QImage>

// 滚动长截图拼接器。
//
// 视口尺寸固定：每帧喂入同一块屏幕区域的截图，用模板匹配算出
// 相对上一帧的滚动偏移（向下滚为正、向上滚为负），把新增内容接到
// 画布底端/顶端。两帧没有重叠（滚动过快）或匹配不可靠时直接丢弃
// 该帧——错位一帧整张长图就废了，宁缺毋滥。
class LongShotStitcher
{
public:
    // 喂入一帧（物理像素尺寸须与之前一致）。画布有增长返回 true；
    // 没滚动 / 匹配不可靠 / 已达最大高度返回 false（可用 isFull 区分）。
    bool feed(const QImage &frame);

    void reset();

    bool isEmpty() const { return m_canvas.isNull(); }
    bool isFull() const { return m_full; }
    QImage canvas() const { return m_canvas; }
    int height() const { return m_canvas.height(); }
    int acceptedFrames() const { return m_accepted; }

private:
    // 相对上一帧的滚动偏移（向下为正）。匹配可靠才返回 true。
    bool matchOffset(const QImage &frame, int *offset) const;
    // 候选偏移的平均绝对差（每采样点 RGB 三通道差值和）
    static qint64 sadAt(const QImage &frame, const QImage &prev, int shift,
                        int stepX, int stepY);

    QImage m_canvas;
    QImage m_lastFrame;
    int m_accepted = 0;
    int m_missStreak = 0;   // 连续匹配失败的帧数（滚动过快防卡死）
    bool m_full = false;

    // 防止无限滚动把内存吃光：20000px 高的 4K 宽图约 300MB，够拼 20 屏
    static constexpr int kMaxCanvasHeight = 20000;
    // 每采样点 RGB 绝对差和的上限（3 通道各 10 级灰阶内视为同一内容）
    static constexpr qint64 kMaxSampleSad = 30;
};
