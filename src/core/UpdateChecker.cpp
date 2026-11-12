#include "core/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace {

// 去掉版本号前面的 v / V，只保留数字与点
QString normalizeVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version = version.mid(1);
    return version;
}

QString downloadDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("FCaptureUpdate"));
}

} // namespace

int compareVersions(const QString &a, const QString &b)
{
    const QStringList left = normalizeVersion(a).split(QLatin1Char('.'));
    const QStringList right = normalizeVersion(b).split(QLatin1Char('.'));

    const int count = qMax(left.size(), right.size());
    for (int i = 0; i < count; ++i) {
        const int l = (i < left.size()) ? left.at(i).toInt() : 0;
        const int r = (i < right.size()) ? right.at(i).toInt() : 0;
        if (l != r)
            return l - r;
    }
    return 0;
}

QString UpdateChecker::assetNamePattern()
{
    // 发布时请使用这个命名：F-Capture-1.1.0-portable.zip
    return QStringLiteral("F-Capture-.*-portable\\.zip");
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiUrl(QStringLiteral("https://api.github.com/repos/zzt2713/FCapture/releases"))
{
}

QString UpdateChecker::parseSha256FromBody(const QString &body)
{
    // 约定：release 说明里写一行 "SHA256: <64位hex>"
    static const QRegularExpression re(
        QStringLiteral("SHA256\\s*[:：]\\s*([0-9a-fA-F]{64})"));
    const auto match = re.match(body);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

void UpdateChecker::check()
{
    if (m_listReply) {
        m_listReply->abort();
        m_listReply->deleteLater();
        m_listReply = nullptr;
    }
    fetchReleases();
}

void UpdateChecker::fetchReleases()
{
    QNetworkRequest request{QUrl(m_apiUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("F-Capture/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/vnd.github+json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);

    m_listReply = m_nam->get(request);
    connect(m_listReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_listReply;
        m_listReply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();

        Release out;
        if (reply->error() != QNetworkReply::NoError) {
            out.error = reply->errorString();
            emit failed(out.error);
            return;
        }

        const QByteArray body = reply->readAll();
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
            out.error = QStringLiteral("无法解析 release 列表");
            emit failed(out.error);
            return;
        }

        const QJsonArray releases = doc.array();
        for (const QJsonValue &value : releases) {
            const QJsonObject release = value.toObject();
            if (release.value(QStringLiteral("draft")).toBool())
                continue;

            const QString tag = release.value(QStringLiteral("tag_name")).toString();
            const QString name = release.value(QStringLiteral("name")).toString();
            const QString version = normalizeVersion(tag.isEmpty() ? name : tag);
            const QString pageUrl = release.value(QStringLiteral("html_url")).toString();
            const QString notes = release.value(QStringLiteral("body")).toString();

            out.version = version;
            out.pageUrl = pageUrl;
            out.notes = notes.left(4000);
            out.sha256 = parseSha256FromBody(notes);
            out.available = compareVersions(version,
                                            QCoreApplication::applicationVersion()) > 0;

            // 找符合命名的安装包资产
            const QRegularExpression pattern(assetNamePattern());
            const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
            for (const QJsonValue &assetValue : assets) {
                const QJsonObject asset = assetValue.toObject();
                const QString assetName = asset.value(QStringLiteral("name")).toString();
                if (!pattern.match(assetName).hasMatch())
                    continue;
                out.assetName = assetName;
                out.assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url"))
                                        .toString());
                break;
            }

            emit finished(out);
            return;
        }

        // 没有任何 release
        emit finished(out);
    });
}

void UpdateChecker::download(const Release &release)
{
    if (!release.assetUrl.isValid()) {
        emit downloadFailed(QStringLiteral("该版本没有可下载的安装包"));
        return;
    }

    cancelDownload();

    QDir dir(downloadDir());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        emit downloadFailed(QStringLiteral("无法创建更新目录：%1").arg(dir.absolutePath()));
        return;
    }
    // 清掉上次残留
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.zip")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());

    m_downloadPath = dir.filePath(release.assetName.isEmpty()
                                      ? QStringLiteral("F-Capture-update.zip")
                                      : release.assetName);

    QNetworkRequest request(release.assetUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("F-Capture/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(120000);

    m_downloadReply = m_nam->get(request);
    QNetworkReply *reply = m_downloadReply;

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit downloadProgress(received, total);
            });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, expectedSha = release.sha256]() {
                if (m_downloadReply != reply)
                    return;
                m_downloadReply = nullptr;
                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    if (reply->error() == QNetworkReply::OperationCanceledError)
                        return;
                    emit downloadFailed(reply->errorString());
                    return;
                }

                QFile file(m_downloadPath);
                if (!file.open(QIODevice::WriteOnly)) {
                    emit downloadFailed(
                        QStringLiteral("无法写入：%1").arg(m_downloadPath));
                    return;
                }
                file.write(reply->readAll());
                file.close();

                // SHA256 校验（release 里提供了才校验）
                if (!expectedSha.isEmpty()) {
                    QFile check(m_downloadPath);
                    if (!check.open(QIODevice::ReadOnly)) {
                        emit downloadFailed(QStringLiteral("无法读取下载文件"));
                        return;
                    }
                    QCryptographicHash hash(QCryptographicHash::Sha256);
                    if (!hash.addData(&check)) {
                        emit downloadFailed(QStringLiteral("校验失败"));
                        return;
                    }
                    const QString actual =
                        QString::fromLatin1(hash.result().toHex());
                    if (actual.compare(expectedSha, Qt::CaseInsensitive) != 0) {
                        QFile::remove(m_downloadPath);
                        emit downloadFailed(
                            QStringLiteral("SHA256 不匹配（文件可能损坏或被篡改）"));
                        return;
                    }
                } else {
                    qWarning() << "UpdateChecker: release 未提供 SHA256，跳过校验";
                }

                emit downloadFinished(m_downloadPath);
            });
}

void UpdateChecker::cancelDownload()
{
    if (!m_downloadReply)
        return;
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->abort();
    reply->deleteLater();
}
