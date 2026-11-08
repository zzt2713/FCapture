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
    OcrClipboard       // 命令行 snip -o ocr_clipboard
};

struct SnipResult
{
    QImage  image;
    QRect   logicalRect;
    bool    valid = false;
};
