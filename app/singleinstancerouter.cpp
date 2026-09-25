#include "singleinstancerouter.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalSocket>
#include <QSharedPointer>
#include <QtDebug>

SingleInstanceRouter::SingleInstanceRouter(const QString& serverName, QObject* parent)
    : QObject(parent),
      m_ServerName(serverName)
{
    connect(&m_Server, &QLocalServer::newConnection,
            this, &SingleInstanceRouter::acceptConnections);
}

QString SingleInstanceRouter::nameForSettingsFile(const QString& settingsFile)
{
    const QByteArray identity =
        QCryptographicHash::hash(settingsFile.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("MoonlightQt-%1").arg(QString::fromLatin1(identity.left(24)));
}

bool SingleInstanceRouter::listen()
{
    if (m_Server.listen(m_ServerName)) {
        return true;
    }
    qInfo() << "Single-instance URI server is unavailable:" << m_Server.errorString();
    return false;
}

bool SingleInstanceRouter::forward(const QString& serverName, const QString& message)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_9);
    stream << message;
    if (payload.size() > MaxMessageBytes) {
        return false;
    }

    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(750)) {
        return false;
    }
    if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1500)) {
        return false;
    }
    socket.disconnectFromServer();
    return true;
}

void SingleInstanceRouter::acceptConnections()
{
    while (m_Server.hasPendingConnections()) {
        QLocalSocket* socket = m_Server.nextPendingConnection();
        if (!socket) {
            continue;
        }
        auto buffer = QSharedPointer<QByteArray>::create();
        connect(socket, &QLocalSocket::readyRead, this, [socket, buffer]() {
            buffer->append(socket->readAll());
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            if (buffer->size() > MaxMessageBytes) {
                emit messageRejected(tr("The forwarded Moonlight URI is too large."));
            }
            else {
                QDataStream stream(buffer.data(), QIODevice::ReadOnly);
                stream.setVersion(QDataStream::Qt_5_9);
                QString message;
                stream >> message;
                if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
                    emit messageRejected(tr("The forwarded Moonlight URI message is invalid."));
                }
                else {
                    emit messageReceived(message);
                }
            }
            socket->deleteLater();
        });
    }
}
