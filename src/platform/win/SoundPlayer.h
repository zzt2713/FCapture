#pragma once

#include <QString>

namespace SoundPlayer {

// 播放随包分发的截图完成音。异步播放，不阻塞截图流程。
bool playCompletionSound(QString *error = nullptr);
void shutdown();

} // namespace SoundPlayer
