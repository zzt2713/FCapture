#pragma once

#include <QObject>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    bool tryConnect(const QStringList &args);
    void listen();

signals:
    void receiveMessage(const QStringList &args);

private:
    void onNewConnection();

    QLocalServer *m_server = nullptr;
    static QString serverName();
};
