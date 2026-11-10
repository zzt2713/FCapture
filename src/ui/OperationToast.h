#pragma once

#include <QString>

namespace OperationToast {

void imageCopied();
void imageSaved();

// 长截图专用提示（受「复制或保存图片后显示提示」开关控制）
void longShotStarted();
void longShotStopped(int frames, int height);

} // namespace OperationToast
