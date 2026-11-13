#pragma once

#include <QImage>
#include <QPixmap>
#include <QRect>

enum class SnipOutput
{
    Copy,
    Save,
    QuickSave,
    Pin,
    FileDialog,
    OcrClipboard,      // 命令行 snip -o ocr_clipboard
    PinWindow          // 钉住窗口：实时镜像选区所在窗口（snow 的 Live Window）
};

struct SnipResult
{
    QImage  image;
    QRect   logicalRect;
    bool    valid = false;
    // 仅 PinWindow 用：要实时镜像的目标窗口句柄
    quintptr pinTargetWindow = 0;
};
