#include "core/ConfigManager.h"

#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

ConfigManager *ConfigManager::instance()
{
    static ConfigManager s_instance;
    return &s_instance;
}

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
    , m_settings(AppPaths::configFile(), QSettings::IniFormat)
{
    // Qt 6 的 QSettings 默认就是 UTF-8，无需 setIniCodec（Qt 5 API 已移除）
    // 便携模式下配置文件在 exe 同目录的 data/ 下，首次运行要保证目录存在
    const QString dir = QFileInfo(AppPaths::configFile()).absolutePath();
    if (!QDir(dir).exists())
        QDir().mkpath(dir);
}

QString ConfigManager::hotkey(const QString &action) const
{
    static const QHash<QString, QString> kDefaults = {
        {QStringLiteral("capture"), QStringLiteral("F1")},
        {QStringLiteral("capture-active-window"), QStringLiteral("Ctrl+Shift+A")},
        {QStringLiteral("paste"), QStringLiteral("F3")},
        {QStringLiteral("toggle-images"), QStringLiteral("Shift+F3")},
        // 长截图：框选区域后滚动拼接长图
        {QStringLiteral("long-screenshot"), QStringLiteral("F2")},
    };
    const QString key = QStringLiteral("hotkey/") + action;
    const QString stored = m_settings.value(key, kDefaults.value(action)).toString().trimmed();
    // 空值一律视为未配置：旧版本曾把默认热键写成空串，导致全部热键失效。
    // 这里直接回落默认值，用户不用手动改配置文件。
    const QString resolved = stored.isEmpty() ? kDefaults.value(action) : stored;

    // 多键存储格式是逗号分隔；这个接口只返回**主键**（第一个），
    // 调用方（注册器/托盘提示/冲突校验）都按单键语义使用。
    const int comma = resolved.indexOf(QLatin1Char(','));
    return comma < 0 ? resolved : resolved.left(comma).trimmed();
}

void ConfigManager::setHotkey(const QString &action, const QString &key)
{
    setHotkeys(action, key.isEmpty() ? QStringList() : QStringList{ key });
}

QStringList ConfigManager::hotkeys(const QString &action) const
{
    // hotkey() 返回的是主键（截断到第一个逗号），这里要完整列表，
    // 所以直接读原始存储值并做同样的默认值回落。
    static const QHash<QString, QString> kDefaults = {
        {QStringLiteral("capture"), QStringLiteral("F1")},
        {QStringLiteral("capture-active-window"), QStringLiteral("Ctrl+Shift+A")},
        {QStringLiteral("paste"), QStringLiteral("F3")},
        {QStringLiteral("toggle-images"), QStringLiteral("Shift+F3")},
        {QStringLiteral("long-screenshot"), QStringLiteral("F2")},
    };
    const QString key = QStringLiteral("hotkey/") + action;
    const QString stored = m_settings.value(key, kDefaults.value(action)).toString().trimmed();
    const QString resolved = stored.isEmpty() ? kDefaults.value(action) : stored;

    QStringList out;
    for (const QString &part : resolved.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && !out.contains(trimmed, Qt::CaseInsensitive))
            out.append(trimmed);
    }
    return out;
}

void ConfigManager::setHotkeys(const QString &action, const QStringList &keys)
{
    const QString fullKey = QStringLiteral("hotkey/") + action;
    QStringList cleaned;
    for (const QString &k : keys) {
        const QString trimmed = k.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed, Qt::CaseInsensitive))
            cleaned.append(trimmed);
    }
    m_settings.setValue(fullKey, cleaned.join(QLatin1Char(',')));
    emit configChanged(fullKey);
}

QVariant ConfigManager::value(const QString &key, const QVariant &fallback) const
{
    static const QHash<QString, QVariant> kDefaults = {
        // 全屏应用（游戏/视频/全屏演示）前台时屏蔽全局快捷键，
        // 避免和游戏按键抢输入。判定逻辑对齐 Snow Shot。
        {QStringLiteral("general/block_hotkeys_in_fullscreen"), true},
        {QStringLiteral("general/auto_save_enabled"), false},
        {QStringLiteral("general/auto_save_folder"), QString()},
        {QStringLiteral("general/quick_save_folder"), QString()},
        {QStringLiteral("general/snip_history_count"), 20},
        {QStringLiteral("general/play_sound"), true},
        {QStringLiteral("general/recover_closed_count"), 1},
        {QStringLiteral("general/copy_image_as_file"), false},
        {QStringLiteral("general/snip_shadow_blur"), 8},
        {QStringLiteral("general/snip_shadow_offset"), 8},
        {QStringLiteral("general/snip_shadow_border"), 1},
        {QStringLiteral("general/never_paste_file_path"), false},
        {QStringLiteral("general/record_all_snips"), false},
        {QStringLiteral("snip/anchor_threshold"), 80},
        {QStringLiteral("snip/show_auxiliary_lines"), true},
        {QStringLiteral("snip/capture_cursor"), false},
        {QStringLiteral("snip/auto_detect_window"), true},
        // 元素/窗口高亮框的过渡动画（对齐 snow 的 SelectionTransitionAnimation）：
        //   0 = 关闭（瞬间切换）   32 = 柔和（101ms）   72 = 快速（67ms）
        {QStringLiteral("snip/selection_transition_animation"), 32},
        {QStringLiteral("snip/shadow_enabled"), false},
        {QStringLiteral("snip/corner_radius"), 0},
        {QStringLiteral("snip/aspect_preset"), QStringLiteral("free")},
        {QStringLiteral("snip/element_msaa_apps"), QString()},
        {QStringLiteral("snip/paste_current_desktop_only"), true},
        {QStringLiteral("interface/theme"), QStringLiteral("dark")},
        {QStringLiteral("interface/uppercase_hex"), false},
        {QStringLiteral("interface/size_unit_dip"), false},
        {QStringLiteral("paste/magnetic_attach_threshold"), 20},
        {QStringLiteral("general/enable_web_image_paste"), true},
        {QStringLiteral("general/hide_tray_icon"), false},
        {QStringLiteral("general/show_operation_toast"), false},
        {QStringLiteral("general/recover_closed_count"), 1},
        {QStringLiteral("general/never_paste_file_path"), false},
        {QStringLiteral("snip/exclude_from_detection"), QString()},
        {QStringLiteral("snip/capture_cursor"), false},
        {QStringLiteral("annotate/auto_select_on_creation"), true},
        {QStringLiteral("diagnostics/export_folder"), QString()},
        {QStringLiteral("output/file_name_format"),
         QStringLiteral("Snipaste_{yyyy}-{MM}-{dd}_{HH}-{mm}-{ss}")},
        {QStringLiteral("output/format"), QStringLiteral("png")},
        {QStringLiteral("output/quality"), -1},
        {QStringLiteral("output/auto_alpha_channel_removal"), false},
        {QStringLiteral("ocr/language"), QStringLiteral("zh-Hans-CN")},
        // OCR 识别增强：对比度预处理 + 可疑时二次识别（默认开）
        {QStringLiteral("ocr/enhance"), true},
        {QStringLiteral("hotcorner/enabled"), false},
        {QStringLiteral("hotcorner/top_left"), QStringLiteral("none")},
        {QStringLiteral("hotcorner/top_right"), QStringLiteral("none")},
        {QStringLiteral("hotcorner/bottom_left"), QStringLiteral("none")},
        {QStringLiteral("hotcorner/bottom_right"), QStringLiteral("none")},
        {QStringLiteral("super_snip/enabled"), false},
        {QStringLiteral("super_snip/modifier"), QStringLiteral("Win")},
        // 不写进 kDefaults 的话 value() 返回无效 QVariant，toInt()=0，
        // SnipHistory 构造时 maxCount 会被 qMax(1,0) 压成 1（只剩一条历史）
        {QStringLiteral("general/snip_history_count"), 20},
    };
    return m_settings.value(key, fallback.isValid() ? fallback : kDefaults.value(key));
}

void ConfigManager::setValue(const QString &key, const QVariant &v)
{
    m_settings.setValue(key, v);
    emit configChanged(key);
}

QString ConfigManager::defaultSaveFolder()
{
    QString pictures =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return QDir::toNativeSeparators(pictures);
}

QString ConfigManager::quickSaveFolder() const
{
    QString folder = value(QStringLiteral("general/quick_save_folder")).toString();
    if (folder.isEmpty()) {
        // 快速保存默认落在图片目录下的 Snipaste 子目录，和 Snipaste 一致
        folder = QDir(defaultSaveFolder()).filePath(QStringLiteral("Snipaste"));
    }
    return folder;
}

QString ConfigManager::autoSaveFolder() const
{
    // 未配置时回落到图片目录：这样「开启自动保存」开箱即可用，
    // 而不是因为目录为空静默什么都不做。
    const QString configured =
        value(QStringLiteral("general/auto_save_folder")).toString();
    return configured.isEmpty() ? defaultSaveFolder() : configured;
}

bool ConfigManager::autoSaveEnabled() const
{
    return value(QStringLiteral("general/auto_save_enabled")).toBool();
}
