#include "platform/win/SoundPlayer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

#include <windows.h>
#include <mmsystem.h>

#include <iterator>

// 完成提示音。
//
// 音源随 exe 内嵌（qrc:/result.mp3），不依赖 dist 里额外拷贝的 sounds/ 目录 ——
// 之前 POST_BUILD 只把 mp3 复制到 build/Release/sounds，而打包只拷 exe 和 DLL，
// 到用户手上就变成了"没有提示音"。
//
// 播放仍走 MCI：Win32 里最稳的 mp3 播放方式，不需要额外 Qt Multimedia 依赖。
// 内嵌资源不能直接交给 MCI（它只认文件路径），所以首次播放时写到临时文件。

namespace {

constexpr wchar_t kAlias[] = L"FCaptureCompletion";
constexpr char kResourcePath[] = ":/result.mp3";

MCIERROR sendCommand(const QString &command)
{
    return mciSendStringW(reinterpret_cast<LPCWSTR>(command.utf16()),
                          nullptr, 0, nullptr);
}

QString mciErrorText(MCIERROR code)
{
    wchar_t buffer[256] = {};
    if (mciGetErrorStringW(code, buffer, static_cast<UINT>(std::size(buffer))))
        return QString::fromWCharArray(buffer);
    return QStringLiteral("MCI error %1").arg(static_cast<qulonglong>(code));
}

void closeCurrent()
{
    sendCommand(QStringLiteral("close ") + QString::fromWCharArray(kAlias));
}

// 把内嵌 mp3 落成临时文件，返回路径；只做一次，进程内复用。
QString ensureSoundFile(QString *error)
{
    static QString cachedPath;
    if (!cachedPath.isEmpty() && QFileInfo::exists(cachedPath))
        return cachedPath;

    QFile res(QString::fromLatin1(kResourcePath));
    if (!res.open(QIODevice::ReadOnly)) {
        // 资源缺失时回退到外部文件（便携包/调试目录里可能仍然存在）
        const QString fallback = QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QStringLiteral("sounds/result.mp3"));
        if (QFileInfo::exists(fallback)) {
            cachedPath = fallback;
            return cachedPath;
        }
        if (error)
            *error = QStringLiteral("找不到内嵌提示音资源");
        return {};
    }

    const QString path = QDir(QDir::tempPath())
                             .filePath(QStringLiteral("FCapture-completion.mp3"));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入提示音临时文件：%1").arg(path);
        return {};
    }
    out.write(res.readAll());
    out.close();

    cachedPath = path;
    return cachedPath;
}

} // namespace

namespace SoundPlayer {

bool playCompletionSound(QString *error)
{
    QString errorText;
    const QString soundPath = ensureSoundFile(&errorText);
    if (soundPath.isEmpty()) {
        if (error)
            *error = errorText;
        return false;
    }

    // 使用同一别名反复播放；每次先关掉旧句柄，避免文件更新后仍缓存旧音频。
    closeCurrent();
    const QString nativePath = QDir::toNativeSeparators(soundPath);
    MCIERROR result = sendCommand(
        QStringLiteral("open \"%1\" type mpegvideo alias %2")
            .arg(nativePath, QString::fromWCharArray(kAlias)));
    if (result != 0) {
        if (error)
            *error = mciErrorText(result);
        return false;
    }

    result = sendCommand(QStringLiteral("play %1 from 0")
                             .arg(QString::fromWCharArray(kAlias)));
    if (result != 0) {
        if (error)
            *error = mciErrorText(result);
        closeCurrent();
        return false;
    }
    return true;
}

void shutdown()
{
    closeCurrent();
}

} // namespace SoundPlayer
