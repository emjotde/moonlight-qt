#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class SingleInstanceRouter : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceRouter(const QString& serverName, QObject* parent = nullptr);

    bool listen();

    static QString nameForSettingsFile(const QString& settingsFile);
    static bool forward(const QString& serverName, const QString& message);

signals:
    void messageReceived(QString message);
    void messageRejected(QString error);

private slots:
    void acceptConnections();

private:
    static constexpr int MaxMessageBytes = 16 * 1024;

    QString m_ServerName;
    QLocalServer m_Server;
};
