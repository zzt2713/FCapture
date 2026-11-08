#pragma once

#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

// 命令行解析。只负责把 argv 变成结构化的请求，不执行任何动作。
//
// 支持的形式（节选，完整列表见 docs/06-P5-工具集成.md 2.3）：
//   snip [--area X Y W H] [--size W H] [--full] [--last] [--active-window]
//        [--active-screen] [--delay SEC] [--shadow|--no-shadow]
//        [-o clipboard|pin|quick-save|file-dialog|ocr_clipboard|FILE]
//        [--hold] [--block [SEC]]
//   paste [--clipboard] [--plain TEXT] [--html TEXT] [--files F...] [--pos X Y]
//   toggle-images / show-images / hide-images
//   create-group [NAME] / switch-group [NAME] / switch-group-prev / empty-group
//   show-group-manager / no-click-through / toggle-click-through
//   whiteboard [--color C] [--active-screen] / transparent-whiteboard
//   pick-color / barcode-scan / barcode-decode FILE
//   open-preferences / docs / exit / --config=PATH
struct CommandRequest
{
    enum class Command {
        None,
        Snip,
        Paste,
        ToggleImages,
        ShowImages,
        HideImages,
        CreateGroup,
        SwitchGroup,
        SwitchGroupPrev,
        EmptyGroup,
        ShowGroupManager,
        Whiteboard,
        PickColor,
        BarcodeScan,
        BarcodeDecode,
        OpenPreferences,
        Docs,
        Exit,
    };

    Command command = Command::None;

    // --- snip ---
    QRect   area;              // --area
    bool    hasArea = false;
    QSize   size;              // --size
    bool    hasSize = false;
    bool    keepAspectRatio = false;
    bool    fullScreen = false;
    bool    lastRegion = false;
    bool    activeWindow = false;
    bool    activeScreen = false;
    int     delayMs = 0;
    bool    shadow = false;
    bool    noShadow = false;
    QStringList outputs;       // -o 的原始值，按出现顺序
    bool    hold = false;
    bool    block = false;
    int     blockTimeoutMs = 0;   // 0 = 不限

    // --- paste ---
    QString pasteText;
    QString pasteHtml;
    QStringList pasteFiles;
    QPoint  pastePos;
    bool    hasPastePos = false;

    // --- 通用 ---
    QString groupName;
    QString color;             // whiteboard --color
    bool    transparentBackground = false;
    QString decodeFile;        // barcode-decode
    QString configPath;        // --config=

    QString error;             // 解析失败原因
    bool ok() const { return command != Command::None && error.isEmpty(); }
};

namespace CommandLine {

// args 是完整 argv（含程序名）；argv[0] 会被忽略
CommandRequest parse(const QStringList &args);

// 把 -o 的值拆成具体动作，按文档固定顺序排序：
// FILE_NAME → file-dialog → printer → quick-save → clipboard → pin → success → exec
QStringList normalizeOutputs(const QStringList &outputs);

} // namespace CommandLine
