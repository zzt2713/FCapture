#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// 检查更新：查询 GitHub Releases 的最新版本。
//
// 设计要点：
//   - 只发一次 GET，不携带任何用户信息（不带设备 ID、不带配置）；
//   - 解析失败/无网络时静默失败，由调用方决定如何提示；
//   - 不用 QNetworkAccessManager 的全局静态实例，避免和其它模块互相影响。
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    // GitHub 仓库的 releases 接口。改仓库时只改这里。
    static QString releasesApiUrl();

    explicit UpdateChecker(QObject *parent = nullptr);

    // 发起一次检查。返回值仅表示"请求已发出"，结果通过信号带回。
    void check();

signals:
    // available=false 时 latestVersion 可能为空（例如没有发布过 release）
    void finished(bool available, const QString &latestVersion,
                  const QString &releaseUrl, const QString &notes);
    void failed(const QString &error);

private:
    void fetchReleases();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_apiUrl;
};

// 版本号比较：a > b 返回正数，相等返回 0，a < b 返回负数。
// 支持 "1.2.3" 形式的点分数字；非数字后缀忽略。
int compareVersions(const QString &a, const QString &b);
