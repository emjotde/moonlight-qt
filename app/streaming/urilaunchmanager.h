#pragma once

#include "settings/streamingpreferences.h"
#include "urilaunchqueue.h"

#include <QObject>

namespace CliStartStream {
class Launcher;
}

class UriLaunchManager : public QObject
{
    Q_OBJECT

public:
    explicit UriLaunchManager(StreamingPreferences* preferences, QObject* parent = nullptr);

    void enqueue(const QString& uri);
    void rejectMessage(const QString& error);

    Q_INVOKABLE void setReady();
    Q_INVOKABLE void releaseLauncher(QObject* launcher);

signals:
    void launchRequested(QObject* launcher);
    void launchError(QString error);
    void activationRequested();

private:
    void processNext();
    StreamingPreferences* m_Preferences;
    UriLaunchQueue m_Queue;
    CliStartStream::Launcher* m_ActiveLauncher = nullptr;
    bool m_Ready = false;
};
