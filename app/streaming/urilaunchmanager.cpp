#include "urilaunchmanager.h"
#include "cli/startstream.h"
#include "urilaunchrequest.h"

#include <QTimer>
#include <QtDebug>

UriLaunchManager::UriLaunchManager(StreamingPreferences* preferences, QObject* parent)
    : QObject(parent),
      m_Preferences(preferences)
{
}

void UriLaunchManager::enqueue(const QString& uri)
{
    emit activationRequested();
    if (!m_Queue.enqueue(uri)) {
        qInfo() << "Ignoring duplicate Moonlight URI";
        return;
    }
    processNext();
}

void UriLaunchManager::rejectMessage(const QString& error)
{
    emit activationRequested();
    emit launchError(error);
}

void UriLaunchManager::setReady()
{
    m_Ready = true;
    m_Queue.setReady();
    processNext();
}

void UriLaunchManager::releaseLauncher(QObject* launcher)
{
    if (launcher != m_ActiveLauncher) {
        return;
    }
    m_ActiveLauncher->deleteLater();
    m_ActiveLauncher = nullptr;
    QTimer::singleShot(0, this, &UriLaunchManager::processNext);
}

void UriLaunchManager::processNext()
{
    if (!m_Ready || m_ActiveLauncher || !m_Queue.hasReadyRequest()) {
        return;
    }

    const QString uri = m_Queue.takeNext();
    const auto result = UriLaunchRequestParser::parse(uri, *m_Preferences);
    if (!result.isValid()) {
        emit launchError(result.error);
        QTimer::singleShot(0, this, &UriLaunchManager::processNext);
        return;
    }

    m_ActiveLauncher = new CliStartStream::Launcher(result.request, this);
    emit launchRequested(m_ActiveLauncher);
}
