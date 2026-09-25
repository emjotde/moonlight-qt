#pragma once

#include <QObject>
#include <QString>

class QThread;

class SingleInstanceRouter : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceRouter(const QString& serverName, QObject* parent = nullptr);
    ~SingleInstanceRouter();

    bool listen();

    static QString nameForSettingsFile(const QString& settingsFile);
    static bool forward(const QString& serverName, const QString& message);

signals:
    void messageReceived(QString message);
    void messageRejected(QString error);

private:
private slots:
    void deliverMessage(QString message);
    void deliverError(QString error);

private:
    QThread* m_ServerThread;
};
