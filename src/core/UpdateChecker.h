#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// 检查更新 + 下载安装包。
//
// 更新流程（方案 B：下载全量 zip → 自替换 → 重启）：
//   1. check() 查询 GitHub Releases 最新版本
//   2. 有新版本时从 release body 里解析 SHA256（形如 "SHA256: <hex>"）
//   3. download() 把 zip 下到 %TEMP%/FCaptureUpdate/
//   4. 校验 SHA256
//   5. UpdateInstaller::install() 写一个后台脚本：等本进程退出 → 解压覆盖 → 重启
//
// 只发 GET，不携带任何用户信息。
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    struct Release
    {
        bool    available = false;   // 是否比当前版本新
        QString version;             // 去掉了 v 前缀，例如 "1.1.0"
        QString pageUrl;             // release 页面
        QString notes;               // release body（截断）
        QString assetName;           // 匹配到的资产文件名
        QUrl    assetUrl;            // 资产下载地址
        QString sha256;              // 期望的 SHA256（空 = 未提供，不校验）
        QString error;               // 非空表示失败
    };

    // 安装包资产的命名约定。发布时请使用 F-Capture-<version>-portable.zip
    static QString assetNamePattern();

    explicit UpdateChecker(QObject *parent = nullptr);

    void check();

    // 下载资产到临时目录；成功时 filePath 指向 zip
    void download(const Release &release);
    void cancelDownload();

signals:
    void finished(const UpdateChecker::Release &release);
    void failed(const QString &error);

    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);

private:
    void fetchReleases();
    static QString parseSha256FromBody(const QString &body);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_listReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QString m_apiUrl;
    QString m_downloadPath;
};

// 版本号比较：a > b 返回正数，相等返回 0，a < b 返回负数。
int compareVersions(const QString &a, const QString &b);
