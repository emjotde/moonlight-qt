#include "singleinstancerouter.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaObject>
#include <QSemaphore>
#include <QSharedPointer>
#include <QThread>
#include <QtDebug>
#include <functional>

namespace {
constexpr int MaxMessageBytes = 16 * 1024;

class SingleInstanceServerThread : public QThread
{
public:
    SingleInstanceServerThread(const QString& serverName, SingleInstanceRouter* router)
        : m_ServerName(serverName),
          m_Router(router)
    {
        setObjectName(QStringLiteral("Moonlight URI IPC"));
    }

    bool startAndWait()
    {
        start();
        m_Started.acquire();
        return m_Listening;
    }

protected:
    void run() override
    {
        QLocalServer server;
        connect(&server, &QLocalServer::newConnection, &server, [this, &server]() {
            acceptConnections(server);
        });
        m_Listening = server.listen(m_ServerName);
        if (!m_Listening) {
            qInfo() << "Single-instance URI server is unavailable:" << server.errorString();
        }
        m_Started.release();
        if (m_Listening) {
            exec();
            server.close();
        }
    }

private:
    void deliver(const char* slot, const QString& value)
    {
        QMetaObject::invokeMethod(m_Router, slot, Qt::QueuedConnection,
                                  Q_ARG(QString, value));
    }

    static void acknowledge(QLocalSocket* socket, bool accepted)
    {
        socket->write(accepted ? "A" : "E", 1);
        socket->flush();
        if (socket->bytesToWrite() > 0) {
            socket->waitForBytesWritten(1500);
        }
        socket->disconnectFromServer();
    }

    void acceptConnections(QLocalServer& server)
    {
        while (server.hasPendingConnections()) {
            QLocalSocket* socket = server.nextPendingConnection();
            if (!socket) {
                continue;
            }
            auto buffer = QSharedPointer<QByteArray>::create();
            auto completed = QSharedPointer<bool>::create(false);
            auto process = QSharedPointer<std::function<void()>>::create();
            *process = [this, socket, buffer, completed]() {
                if (*completed || buffer->size() < static_cast<int>(sizeof(quint32))) {
                    return;
                }
                QDataStream stream(buffer.data(), QIODevice::ReadOnly);
                stream.setByteOrder(QDataStream::BigEndian);
                quint32 payloadSize;
                stream >> payloadSize;
                if (payloadSize > MaxMessageBytes) {
                    *completed = true;
                    deliver("deliverError", tr("The forwarded Moonlight URI is too large."));
                    acknowledge(socket, false);
                    return;
                }
                const int expectedSize = static_cast<int>(sizeof(quint32) + payloadSize);
                if (buffer->size() < expectedSize) {
                    return;
                }
                *completed = true;
                if (buffer->size() != expectedSize) {
                    deliver("deliverError",
                            tr("The forwarded Moonlight URI message is invalid."));
                    acknowledge(socket, false);
                    return;
                }

                const QByteArray payload = buffer->mid(sizeof(quint32), payloadSize);
                const QString message = QString::fromUtf8(payload);
                if (message.toUtf8() != payload) {
                    deliver("deliverError",
                            tr("The forwarded Moonlight URI is not valid UTF-8."));
                    acknowledge(socket, false);
                    return;
                }
                deliver("deliverMessage", message);
                acknowledge(socket, true);
            };
            connect(socket, &QLocalSocket::readyRead, socket,
                    [socket, buffer, process]() {
                buffer->append(socket->readAll());
                (*process)();
            });
            connect(socket, &QLocalSocket::disconnected, socket,
                    [this, socket, buffer, completed, process]() {
                buffer->append(socket->readAll());
                (*process)();
                if (!*completed) {
                    *completed = true;
                    deliver("deliverError",
                            tr("The forwarded Moonlight URI message was incomplete."));
                }
                socket->deleteLater();
            });
            buffer->append(socket->readAll());
            (*process)();
        }
    }

    QString m_ServerName;
    SingleInstanceRouter* m_Router;
    QSemaphore m_Started;
    bool m_Listening = false;
};
}

SingleInstanceRouter::SingleInstanceRouter(const QString& serverName, QObject* parent)
    : QObject(parent),
      m_ServerThread(new SingleInstanceServerThread(serverName, this))
{
}

SingleInstanceRouter::~SingleInstanceRouter()
{
    if (m_ServerThread->isRunning()) {
        m_ServerThread->quit();
        m_ServerThread->wait();
    }
    delete m_ServerThread;
}

QString SingleInstanceRouter::nameForSettingsFile(const QString& settingsFile)
{
    const QByteArray identity =
        QCryptographicHash::hash(settingsFile.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("MoonlightQt-%1").arg(QString::fromLatin1(identity.left(24)));
}

bool SingleInstanceRouter::listen()
{
    return static_cast<SingleInstanceServerThread*>(m_ServerThread)->startAndWait();
}

bool SingleInstanceRouter::forward(const QString& serverName, const QString& message)
{
    const QByteArray payload = message.toUtf8();
    if (payload.size() > MaxMessageBytes) {
        return false;
    }
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(payload.size());
    frame.append(payload);

    QLocalSocket socket;
    socket.connectToServer(serverName, QIODevice::ReadWrite);
    if (!socket.waitForConnected(750)) {
        return false;
    }
    if (socket.write(frame) != frame.size()) {
        return false;
    }
    socket.flush();
    if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(1500)) {
        return false;
    }
    if (!socket.waitForReadyRead(2000)) {
        return false;
    }
    return socket.read(1) == QByteArrayLiteral("A");
}

void SingleInstanceRouter::deliverMessage(QString message)
{
    emit messageReceived(message);
}

void SingleInstanceRouter::deliverError(QString error)
{
    emit messageRejected(error);
}
