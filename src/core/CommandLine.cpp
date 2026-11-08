#include "core/CommandLine.h"

#include <QFileInfo>

namespace {

// 取下一个参数，越界返回空
QString takeArg(const QStringList &args, int *index)
{
    if (*index + 1 >= args.size())
        return {};
    return args.at(++(*index));
}

bool takeInt(const QStringList &args, int *index, int *out)
{
    const QString s = takeArg(args, index);
    bool ok = false;
    const int v = s.toInt(&ok);
    if (ok)
        *out = v;
    return ok;
}

} // namespace

namespace CommandLine {

CommandRequest parse(const QStringList &args)
{
    CommandRequest req;
    if (args.size() < 2) {
        req.error = QStringLiteral("缺少命令");
        return req;
    }

    // 子命令 = 第一个非 -- 开头的参数
    int i = 1;
    QString command = args.at(i).toLower();
    if (command.startsWith(QLatin1Char('-'))) {
        // `FCapture --config=x snip` 这种形式：先跳过全局选项
        command.clear();
    } else {
        ++i;
    }

    if (command == QStringLiteral("snip"))            req.command = CommandRequest::Command::Snip;
    else if (command == QStringLiteral("paste"))      req.command = CommandRequest::Command::Paste;
    else if (command == QStringLiteral("toggle-images")) req.command = CommandRequest::Command::ToggleImages;
    else if (command == QStringLiteral("show-images"))   req.command = CommandRequest::Command::ShowImages;
    else if (command == QStringLiteral("hide-images"))   req.command = CommandRequest::Command::HideImages;
    else if (command == QStringLiteral("create-group"))  req.command = CommandRequest::Command::CreateGroup;
    else if (command == QStringLiteral("switch-group"))  req.command = CommandRequest::Command::SwitchGroup;
    else if (command == QStringLiteral("switch-groups")) req.command = CommandRequest::Command::SwitchGroupPrev;
    else if (command == QStringLiteral("switch-group-prev")) req.command = CommandRequest::Command::SwitchGroupPrev;
    else if (command == QStringLiteral("empty-group"))   req.command = CommandRequest::Command::EmptyGroup;
    else if (command == QStringLiteral("show-group-manager")) req.command = CommandRequest::Command::ShowGroupManager;
    else if (command == QStringLiteral("whiteboard"))     req.command = CommandRequest::Command::Whiteboard;
    else if (command == QStringLiteral("transparent-whiteboard")) {
        req.command = CommandRequest::Command::Whiteboard;
        req.transparentBackground = true;
    }
    else if (command == QStringLiteral("pick-color"))     req.command = CommandRequest::Command::PickColor;
    else if (command == QStringLiteral("barcode-scan"))   req.command = CommandRequest::Command::BarcodeScan;
    else if (command == QStringLiteral("barcode-decode")) req.command = CommandRequest::Command::BarcodeDecode;
    else if (command == QStringLiteral("open-preferences")) req.command = CommandRequest::Command::OpenPreferences;
    else if (command == QStringLiteral("docs"))           req.command = CommandRequest::Command::Docs;
    else if (command == QStringLiteral("exit"))           req.command = CommandRequest::Command::Exit;

    // ---- 参数 ----
    for (; i < args.size(); ++i) {
        const QString a = args.at(i);
        const QString lower = a.toLower();

        if (lower.startsWith(QStringLiteral("--config="))) {
            req.configPath = a.mid(a.indexOf(QLatin1Char('=')) + 1);
        } else if (lower == QStringLiteral("--area")) {
            int x = 0, y = 0, w = 0, h = 0;
            if (!takeInt(args, &i, &x) || !takeInt(args, &i, &y)
                || !takeInt(args, &i, &w) || !takeInt(args, &i, &h)) {
                req.error = QStringLiteral("--area 需要 4 个整数");
                return req;
            }
            req.area = QRect(x, y, w, h);
            req.hasArea = true;
        } else if (lower == QStringLiteral("--size")) {
            int w = 0, h = 0;
            if (!takeInt(args, &i, &w) || !takeInt(args, &i, &h)) {
                req.error = QStringLiteral("--size 需要 2 个整数");
                return req;
            }
            req.size = QSize(w, h);
            req.hasSize = true;
        } else if (lower == QStringLiteral("--keep_aspect_ratio")) {
            req.keepAspectRatio = true;
        } else if (lower == QStringLiteral("--full")) {
            req.fullScreen = true;
        } else if (lower == QStringLiteral("--last")) {
            req.lastRegion = true;
        } else if (lower == QStringLiteral("--active-window")) {
            req.activeWindow = true;
        } else if (lower == QStringLiteral("--active-screen")) {
            req.activeScreen = true;
        } else if (lower == QStringLiteral("--delay")) {
            int sec = 0;
            if (!takeInt(args, &i, &sec)) {
                req.error = QStringLiteral("--delay 需要秒数");
                return req;
            }
            req.delayMs = qMax(0, sec) * 1000;
        } else if (lower == QStringLiteral("--shadow")) {
            req.shadow = true;
        } else if (lower == QStringLiteral("--no-shadow")) {
            req.noShadow = true;
        } else if (lower == QStringLiteral("--hold")) {
            req.hold = true;
        } else if (lower == QStringLiteral("--block")) {
            req.block = true;
            // 可选秒数
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-'))) {
                bool ok = false;
                const int sec = args.at(i + 1).toInt(&ok);
                if (ok) {
                    ++i;
                    req.blockTimeoutMs = qMax(0, sec) * 1000;
                }
            }
        } else if (lower == QStringLiteral("-o") || lower == QStringLiteral("--output")) {
            const QString value = takeArg(args, &i);
            if (value.isEmpty()) {
                req.error = QStringLiteral("-o 需要一个值");
                return req;
            }
            // 支持 "file-dialog;clipboard" 这种一次给多个
            for (const QString &part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
                req.outputs.append(part.trimmed());
        } else if (lower == QStringLiteral("--clipboard")) {
            req.outputs.append(QStringLiteral("clipboard"));
        } else if (lower == QStringLiteral("--plain")) {
            req.pasteText = takeArg(args, &i);
        } else if (lower == QStringLiteral("--html")) {
            req.pasteHtml = takeArg(args, &i);
        } else if (lower == QStringLiteral("--files")) {
            // 吸收后面所有非选项参数
            while (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-')))
                req.pasteFiles.append(args.at(++i));
        } else if (lower == QStringLiteral("--pos")) {
            int x = 0, y = 0;
            if (!takeInt(args, &i, &x) || !takeInt(args, &i, &y)) {
                req.error = QStringLiteral("--pos 需要 2 个整数");
                return req;
            }
            req.pastePos = QPoint(x, y);
            req.hasPastePos = true;
        } else if (lower == QStringLiteral("--color")) {
            req.color = takeArg(args, &i);
        } else if (!a.startsWith(QLatin1Char('-'))) {
            // 位置参数：分组名 / 解码文件
            if (req.command == CommandRequest::Command::CreateGroup
                || req.command == CommandRequest::Command::SwitchGroup) {
                req.groupName = a;
            } else if (req.command == CommandRequest::Command::BarcodeDecode) {
                req.decodeFile = a;
            }
        }
    }

    if (req.command == CommandRequest::Command::None) {
        req.error = QStringLiteral("未知命令：%1").arg(args.at(1));
    } else if (req.command == CommandRequest::Command::BarcodeDecode && req.decodeFile.isEmpty()) {
        req.error = QStringLiteral("barcode-decode 需要文件路径");
    }

    return req;
}

QStringList normalizeOutputs(const QStringList &outputs)
{
    // 固定顺序：FILE_NAME → file-dialog → printer → quick-save
    //           → clipboard → pin → success → exec
    QStringList files;
    QStringList dialog;
    QStringList printer;
    QStringList quickSave;
    QStringList clipboard;
    QStringList pin;
    QStringList success;
    QStringList exec;

    for (const QString &o : outputs) {
        const QString lower = o.toLower();
        if (lower == QStringLiteral("file-dialog"))   dialog << o;
        else if (lower == QStringLiteral("printer"))  printer << o;
        else if (lower == QStringLiteral("quick-save")) quickSave << o;
        else if (lower == QStringLiteral("clipboard")) clipboard << o;
        else if (lower == QStringLiteral("ocr_clipboard")) clipboard << o;
        else if (lower == QStringLiteral("pin"))      pin << o;
        else if (lower == QStringLiteral("success"))  success << o;
        else if (lower == QStringLiteral("silent"))   success << o;
        else if (lower == QStringLiteral("no-auto-save")) success << o;
        else if (lower.startsWith(QStringLiteral("exec("))) exec << o;
        else files << o;   // 其余视为文件路径
    }

    QStringList ordered;
    ordered << files << dialog << printer << quickSave
            << clipboard << pin << success << exec;
    return ordered;
}

} // namespace CommandLine
