#include "core/SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

QString SingleInstance::serverName()
{
    return QStringLiteral("FCapture-Instance");
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstance::tryConnect(const QStringList &args)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(300))
        return false;

    QJsonArray arr;
    for (const QString &a : args)
        arr.append(a);
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("args"), arr}}).toJson(QJsonDocument::Compact);

    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);

    if (!m_server->listen(serverName())) {
        // 残留的 socket 文件，清理后重试
        QLocalServer::removeServer(serverName());
        if (!m_server->listen(serverName())) {
            qWarning() << "SingleInstance: failed to listen:" << m_server->errorString();
            return;
        }
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        if (!socket->waitForReadyRead(500)) {
            socket->deleteLater();
            continue;
        }
        const QByteArray payload = socket->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(payload);
        const QJsonArray arr = doc.object().value(QStringLiteral("args")).toArray();

        QStringList args;
        args.reserve(arr.size());
        for (const auto &v : arr)
            args.append(v.toString());

        emit receiveMessage(args);
        socket->disconnectFromServer();
    }
}
