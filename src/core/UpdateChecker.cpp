#include "core/UpdateChecker.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
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

} // namespace

int compareVersions(const QString &a, const QString &b)
{
    const QStringList left = normalizeVersion(a).split(QLatin1Char('.'));
    const QStringList right = normalizeVersion(b).split(QLatin1Char('.'));

    const int count = qMax(left.size(), right.size());
    for (int i = 0; i < count; ++i) {
        // 非数字段按 0 处理，保证 "1.0" 与 "1.0.0" 相等
        const int l = (i < left.size()) ? left.at(i).toInt() : 0;
        const int r = (i < right.size()) ? right.at(i).toInt() : 0;
        if (l != r)
            return l - r;
    }
    return 0;
}

QString UpdateChecker::releasesApiUrl()
{
    // 仓库地址固定；换仓库只改这一处
    return QStringLiteral("https://api.github.com/repos/zzt2713/F_Capture/releases");
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiUrl(releasesApiUrl())
{
}

void UpdateChecker::check()
{
    // 上一次还没回来就先放弃旧的，避免结果错配
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    fetchReleases();
}

void UpdateChecker::fetchReleases()
{
    QNetworkRequest request{QUrl(m_apiUrl)};
    // GitHub API 要求带 User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("F-Capture/%1")
                          .arg(QCoreApplication::applicationVersion()));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/vnd.github+json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit failed(reply->errorString());
            return;
        }

        const QByteArray body = reply->readAll();
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit failed(QStringLiteral("返回内容不是合法 JSON：%1")
                            .arg(parseError.errorString()));
            return;
        }
        if (!doc.isArray()) {
            emit failed(QStringLiteral("返回内容不是 release 列表"));
            return;
        }

        const QJsonArray releases = doc.array();
        if (releases.isEmpty()) {
            emit finished(false, QString(), QString(), QString());
            return;
        }

        // 取最新一条（GitHub 已按发布时间倒序，但草稿/预发布要跳过）
        for (const QJsonValue &value : releases) {
            const QJsonObject release = value.toObject();
            if (release.value(QStringLiteral("draft")).toBool())
                continue;

            const QString tag = release.value(QStringLiteral("tag_name")).toString();
            const QString name = release.value(QStringLiteral("name")).toString();
            const QString url = release.value(QStringLiteral("html_url")).toString();
            const QString notes = release.value(QStringLiteral("body")).toString();
            const QString version = tag.isEmpty() ? name : tag;

            const QString current = QCoreApplication::applicationVersion();
            const bool newer = compareVersions(version, current) > 0;
            emit finished(newer, normalizeVersion(version), url, notes.left(4000));
            return;
        }

        emit finished(false, QString(), QString(), QString());
    });
}
