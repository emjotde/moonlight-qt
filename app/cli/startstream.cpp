#include "startstream.h"
#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include "settings/appstreamingsettings.h"
#include "settings/externallaunchtrust.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

#include <QSettings>
#include <QTimer>

#define COMPUTER_SEEK_TIMEOUT 30000
#define APP_SEEK_TIMEOUT 10000

namespace CliStartStream
{

enum State {
    StateInit,
    StateSeekComputer,
    StatePairing,
    StateSeekApp,
    StateConfirmExternalLaunch,
    StateWaitForQuitApproval,
    StateWaitForQuitCompletion,
    StateStartSession,
    StateFailure,
};

class Event
{
public:
    enum Type {
        AppQuitCompleted,
        AppQuitRequested,
        ComputerFound,
        ComputerUpdated,
        Executed,
        Timedout,
    };

    Event(Type type)
        : type(type), computerManager(nullptr), computer(nullptr) {}

    Type type;
    ComputerManager *computerManager;
    NvComputer *computer;
    QString errorMessage;
};

class LauncherPrivate
{
    Q_DECLARE_PUBLIC(Launcher)

public:
    LauncherPrivate(Launcher *q)
        : q_ptr(q),
          m_ComputerManager(nullptr),
          m_ComputerSeeker(nullptr),
          m_Computer(nullptr),
          m_State(StateInit),
          m_TimeoutTimer(nullptr)
    {
    }

    void fail(const QString& message)
    {
        Q_Q(Launcher);
        m_State = StateFailure;
        if (m_TimeoutTimer) {
            m_TimeoutTimer->stop();
        }
        emit q->failed(message);
    }

    void beginAppSearch()
    {
        Q_Q(Launcher);
        m_State = StateSeekApp;
        m_TimeoutTimer->start(APP_SEEK_TIMEOUT);
        emit q->searchingApp();
        processAppList();
    }

    QString displayModeName(StreamingPreferences::WindowMode mode) const
    {
        switch (mode) {
        case StreamingPreferences::WM_FULLSCREEN:
            return QObject::tr("Fullscreen");
        case StreamingPreferences::WM_FULLSCREEN_DESKTOP:
            return QObject::tr("Borderless fullscreen");
        case StreamingPreferences::WM_WINDOWED:
            return QObject::tr("Windowed");
        }
        return QObject::tr("Unknown");
    }

    QString confirmationDetails(const NvApp& app) const
    {
        QSettings settings;
        const auto profile = AppStreamingSettings::load(settings, m_Computer->uuid, app.id);
        QScopedPointer<StreamingPreferences> preferences(
            StreamLaunchPreferences::resolve(*StreamingPreferences::get(), profile, m_Request));
        return QObject::tr("Host: %1\n"
                           "Application: %2\n"
                           "Resolution: %3 x %4 at %5 FPS\n"
                           "Display mode: %6")
            .arg(m_Computer->name,
                 app.name,
                 QString::number(preferences->width),
                 QString::number(preferences->height),
                 QString::number(preferences->fps),
                 displayModeName(preferences->windowMode));
    }

    void createSession(const NvApp& app)
    {
        Q_Q(Launcher);
        m_State = StateStartSession;
        auto session = new Session(m_Computer, app, m_Request);
        emit q->sessionCreated(app.name, session);
    }

    void prepareLaunch(const NvApp& app)
    {
        Q_Q(Launcher);
        if (m_Request.source == StreamLaunchRequest::UriSource) {
            QSettings settings;
            const bool trusted = ExternalLaunchTrust::isTrusted(settings, m_Computer->uuid);
            if (ExternalLaunchTrust::shouldConfirm(
                    StreamingPreferences::get()->confirmExternalLaunchRequests,
                    m_Computer->pairState == NvComputer::PS_PAIRED,
                    trusted)) {
                m_PendingApp = app;
                m_State = StateConfirmExternalLaunch;
                emit q->externalLaunchConfirmationRequired(confirmationDetails(app));
                return;
            }
        }
        createSession(app);
    }

    void processAppList()
    {
        Q_Q(Launcher);
        if (m_State != StateSeekApp || !m_Computer) {
            return;
        }
        const int index = m_Request.findAppIndex(m_Computer->appList);
        if (index < 0) {
            return;
        }

        const NvApp app = m_Computer->appList[index];
        m_TimeoutTimer->stop();
        if (m_Computer->currentGameId != 0 && m_Computer->currentGameId != app.id) {
            if (m_Request.source == StreamLaunchRequest::UriSource) {
                fail(QObject::tr("A different application is already active on %1. "
                                 "End that stream before opening this link.")
                         .arg(m_Computer->name));
            }
            else {
                m_PendingApp = app;
                m_State = StateWaitForQuitApproval;
                emit q->appQuitRequired(getCurrentAppName());
            }
            return;
        }
        prepareLaunch(app);
    }

    QString getCurrentAppName() const
    {
        for (const NvApp& app : m_Computer->appList) {
            if (m_Computer->currentGameId == app.id) {
                return app.name;
            }
        }
        return QObject::tr("the current application");
    }

    void handleEvent(Event event)
    {
        Q_Q(Launcher);
        switch (event.type) {
        case Event::Executed:
            if (m_State == StateInit) {
                m_State = StateSeekComputer;
                m_ComputerManager = event.computerManager;
                m_ComputerSeeker = new ComputerSeeker(m_ComputerManager, m_Request.host, q);
                q->connect(m_ComputerSeeker, &ComputerSeeker::computerFound,
                           q, &Launcher::onComputerFound);
                q->connect(m_ComputerSeeker, &ComputerSeeker::errorTimeout,
                           q, &Launcher::onTimeout);
                q->connect(m_ComputerManager, &ComputerManager::computerStateChanged,
                           q, &Launcher::onComputerUpdated);
                q->connect(m_ComputerManager, &ComputerManager::quitAppCompleted,
                           q, &Launcher::onQuitAppCompleted);
                q->connect(m_ComputerManager, &ComputerManager::pairingCompleted,
                           q, &Launcher::onPairingCompleted);
                m_ComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);
                emit q->searchingComputer();
            }
            break;
        case Event::ComputerFound:
            if (m_State != StateSeekComputer) {
                break;
            }
            m_Computer = event.computer;
            if (m_Computer->pairState == NvComputer::PS_PAIRED) {
                beginAppSearch();
            }
            else if (m_Request.source == StreamLaunchRequest::UriSource) {
                m_State = StatePairing;
                const QString pin = m_ComputerManager->generatePinString();
                m_ComputerManager->pairHost(m_Computer, pin);
                emit q->pairingRequired(m_Computer->name, pin);
            }
            else {
                fail(QObject::tr("Computer %1 has not been paired. "
                                 "Please open Moonlight to pair before streaming.")
                         .arg(m_Computer->name));
            }
            break;
        case Event::ComputerUpdated:
            if (event.computer != m_Computer) {
                break;
            }
            if (m_State == StateSeekApp) {
                processAppList();
            }
            else if (m_State == StateWaitForQuitCompletion &&
                     m_Computer->currentGameId == 0) {
                beginAppSearch();
            }
            break;
        case Event::AppQuitRequested:
            if (m_State == StateWaitForQuitApproval) {
                m_State = StateWaitForQuitCompletion;
                m_ComputerManager->quitRunningApp(m_Computer);
            }
            break;
        case Event::AppQuitCompleted:
            if (m_State == StateWaitForQuitCompletion) {
                if (!event.errorMessage.isEmpty()) {
                    fail(QObject::tr("Quitting app failed, reason: %1").arg(event.errorMessage));
                }
                else if (m_Computer->currentGameId == 0) {
                    beginAppSearch();
                }
            }
            break;
        case Event::Timedout:
            if (m_State == StateSeekComputer) {
                NvComputer* known =
                    ComputerSeeker::findComputer(m_ComputerManager, m_Request.host);
                if (known) {
                    fail(QObject::tr("Host %1 is offline.").arg(known->name));
                }
                else {
                    fail(QObject::tr("Unknown host: %1").arg(m_Request.host));
                }
            }
            else if (m_State == StateSeekApp) {
                fail(QObject::tr("Failed to find %1 on %2.")
                         .arg(m_Request.appDescription(), m_Computer->name));
            }
            break;
        }
    }

    void approveExternalLaunch(bool alwaysAllow)
    {
        if (m_State != StateConfirmExternalLaunch) {
            return;
        }
        if (alwaysAllow) {
            QSettings settings;
            const QString error = ExternalLaunchTrust::trustHost(settings, m_Computer->uuid);
            if (!error.isEmpty()) {
                fail(error);
                return;
            }
        }
        createSession(m_PendingApp);
    }

    void cancelExternalLaunch()
    {
        Q_Q(Launcher);
        if (m_State == StateConfirmExternalLaunch) {
            m_State = StateFailure;
            emit q->externalLaunchCancelled();
        }
    }

    Launcher *q_ptr;
    StreamLaunchRequest m_Request;
    ComputerManager *m_ComputerManager;
    ComputerSeeker *m_ComputerSeeker;
    NvComputer *m_Computer;
    NvApp m_PendingApp;
    State m_State;
    QTimer *m_TimeoutTimer;
};

Launcher::Launcher(const StreamLaunchRequest& request, QObject *parent)
    : QObject(parent),
      m_DPtr(new LauncherPrivate(this))
{
    Q_D(Launcher);
    d->m_Request = request;
    d->m_TimeoutTimer = new QTimer(this);
    d->m_TimeoutTimer->setSingleShot(true);
    connect(d->m_TimeoutTimer, &QTimer::timeout,
            this, &Launcher::onTimeout);
}

Launcher::~Launcher()
{
}

void Launcher::execute(ComputerManager *manager)
{
    Q_D(Launcher);
    Event event(Event::Executed);
    event.computerManager = manager;
    d->handleEvent(event);
}

void Launcher::quitRunningApp()
{
    Q_D(Launcher);
    d->handleEvent(Event(Event::AppQuitRequested));
}

void Launcher::approveExternalLaunch(bool alwaysAllow)
{
    Q_D(Launcher);
    d->approveExternalLaunch(alwaysAllow);
}

void Launcher::cancelExternalLaunch()
{
    Q_D(Launcher);
    d->cancelExternalLaunch();
}

bool Launcher::isExecuted() const
{
    Q_D(const Launcher);
    return d->m_State != StateInit;
}

void Launcher::onComputerFound(NvComputer *computer)
{
    Q_D(Launcher);
    Event event(Event::ComputerFound);
    event.computer = computer;
    d->handleEvent(event);
}

void Launcher::onComputerUpdated(NvComputer *computer)
{
    Q_D(Launcher);
    Event event(Event::ComputerUpdated);
    event.computer = computer;
    d->handleEvent(event);
}

void Launcher::onTimeout()
{
    Q_D(Launcher);
    d->handleEvent(Event(Event::Timedout));
}

void Launcher::onQuitAppCompleted(QVariant error)
{
    Q_D(Launcher);
    Event event(Event::AppQuitCompleted);
    event.errorMessage = error.toString();
    d->handleEvent(event);
}

void Launcher::onPairingCompleted(NvComputer* computer, QString error)
{
    Q_D(Launcher);
    if (d->m_State != StatePairing || computer != d->m_Computer) {
        return;
    }
    if (!error.isEmpty()) {
        d->fail(tr("Pairing failed: %1").arg(error));
        return;
    }
    emit pairingFinished();
    d->beginAppSearch();
}

}
