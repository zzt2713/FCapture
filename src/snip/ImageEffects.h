#pragma once

#include <QImage>
#include <QString>

// 导出阶段的图像效果：圆角、阴影、拍平到白底
namespace ImageEffects {

// 四角做成圆角，圆角外透明。radius 为物理像素。
QImage applyRoundedCorners(const QImage &src, int radius);

// 截图内容加模糊投影 + 可选描边（类似 macOS 窗口截图）。
// blur/offset/border 均为物理像素；结果四周会变大。
QImage applyShadow(const QImage &src, int blur, int offset, int border);

// 把透明像素合成到白底上（JPEG/BMP 不支持 alpha 时用）
QImage flattenOnWhite(const QImage &src);

// 圆角半径按选区短边的比例上限钳制，避免小图被切成圆形
int clampCornerRadius(int radius, const QSize &size);

} // namespace ImageEffects
