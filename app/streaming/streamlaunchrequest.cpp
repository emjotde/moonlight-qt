#include "streamlaunchrequest.h"
#include "backend/nvapp.h"

#include <QtDebug>

StreamLaunchOverrides::StreamLaunchOverrides(const StreamingPreferences& base)
    : m_Values(new StreamingPreferences(base))
{
}

StreamingPreferences* StreamLaunchOverrides::values()
{
    return m_Values.data();
}

const StreamingPreferences* StreamLaunchOverrides::values() const
{
    return m_Values.data();
}

quint64 StreamLaunchOverrides::fieldMask(Field field)
{
    Q_ASSERT(field >= 0 && field < FieldCount);
    return quint64(1) << static_cast<unsigned int>(field);
}

void StreamLaunchOverrides::markExplicit(Field field)
{
    Q_ASSERT(m_Values);
    m_ExplicitFields |= fieldMask(field);
}

void StreamLaunchOverrides::markDerived(Field field)
{
    Q_ASSERT(m_Values);
    m_DerivedFields |= fieldMask(field);
}

bool StreamLaunchOverrides::isExplicit(Field field) const
{
    return (m_ExplicitFields & fieldMask(field)) != 0;
}

bool StreamLaunchOverrides::isDerived(Field field) const
{
    return (m_DerivedFields & fieldMask(field)) != 0;
}

bool StreamLaunchOverrides::isEmpty() const
{
    return m_ExplicitFields == 0 && m_DerivedFields == 0;
}

void StreamLaunchOverrides::applyField(StreamingPreferences& target, Field field) const
{
    Q_ASSERT(m_Values);
    const auto& source = *m_Values;
    switch (field) {
    case Resolution:
        target.width = source.width;
        target.height = source.height;
        break;
    case Fps:
        target.fps = source.fps;
        break;
    case Bitrate:
        target.bitrateKbps = source.bitrateKbps;
        break;
    case PacketSize:
        target.packetSize = source.packetSize;
        break;
    case WindowMode:
        target.windowMode = source.windowMode;
        break;
    case Vsync:
        target.enableVsync = source.enableVsync;
        break;
    case AudioConfig:
        target.audioConfig = source.audioConfig;
        break;
    case MultiController:
        target.multiController = source.multiController;
        break;
    case QuitAfter:
        target.quitAppAfter = source.quitAppAfter;
        break;
    case AbsoluteMouse:
        target.absoluteMouseMode = source.absoluteMouseMode;
        break;
    case SwapMouseButtons:
        target.swapMouseButtons = source.swapMouseButtons;
        break;
    case AbsoluteTouch:
        target.absoluteTouchMode = source.absoluteTouchMode;
        break;
    case GameOptimizations:
        target.gameOptimizations = source.gameOptimizations;
        break;
    case PlayAudioOnHost:
        target.playAudioOnHost = source.playAudioOnHost;
        break;
    case FramePacing:
        target.framePacing = source.framePacing;
        break;
    case MuteOnFocusLoss:
        target.muteOnFocusLoss = source.muteOnFocusLoss;
        break;
    case BackgroundGamepad:
        target.backgroundGamepad = source.backgroundGamepad;
        break;
    case ReverseScroll:
        target.reverseScrollDirection = source.reverseScrollDirection;
        break;
    case SwapFaceButtons:
        target.swapFaceButtons = source.swapFaceButtons;
        break;
    case KeepAwake:
        target.keepAwake = source.keepAwake;
        break;
    case PerformanceOverlay:
        target.showPerformanceOverlay = source.showPerformanceOverlay;
        break;
    case Hdr:
        target.enableHdr = source.enableHdr;
        break;
    case Yuv444:
        target.enableYUV444 = source.enableYUV444;
        break;
    case CaptureSystemKeys:
        target.captureSysKeysMode = source.captureSysKeysMode;
        break;
    case VideoCodec:
        target.videoCodecConfig = source.videoCodecConfig;
        break;
    case VideoDecoder:
        target.videoDecoderSelection = source.videoDecoderSelection;
        break;
    case FieldCount:
        Q_UNREACHABLE();
    }
}

void StreamLaunchOverrides::applyTo(StreamingPreferences& target, bool includeDerived) const
{
    if (!m_Values) {
        return;
    }

    const quint64 fields = m_ExplicitFields | (includeDerived ? m_DerivedFields : 0);
    for (int field = 0; field < FieldCount; ++field) {
        const auto typedField = static_cast<Field>(field);
        if ((fields & fieldMask(typedField)) != 0) {
            applyField(target, typedField);
        }
    }
}

void StreamLaunchPreferences::applyProfile(StreamingPreferences& target,
                                           const StreamingPreferences& global,
                                           const AppStreamingOverride& profile)
{
    if (!profile.enabled) {
        return;
    }

    const QString error = AppStreamingSettings::validate(profile);
    if (!error.isEmpty()) {
        qWarning() << "Ignoring invalid application stream settings:" << error;
        return;
    }

    target.width = profile.width;
    target.height = profile.height;
    target.fps = profile.fps;
    target.bitrateKbps = profile.bitrateKbps != 0 ? profile.bitrateKbps : global.bitrateKbps;
    if (profile.windowMode >= 0) {
        target.windowMode = static_cast<StreamingPreferences::WindowMode>(profile.windowMode);
    }
    if (profile.captureSysKeysMode >= 0) {
        target.captureSysKeysMode =
            static_cast<StreamingPreferences::CaptureSysKeysMode>(profile.captureSysKeysMode);
    }
    target.preferredDisplay = profile.preferredDisplay;
}

StreamingPreferences* StreamLaunchPreferences::resolve(const StreamingPreferences& global,
                                                       const AppStreamingOverride& profile,
                                                       const StreamLaunchRequest& request)
{
    auto effective = new StreamingPreferences(global);
    applyProfile(*effective, global, profile);

    // Preserve the legacy CLI default-bitrate calculation only when no saved
    // application profile exists. Explicit CLI bitrate always wins.
    request.cliOverrides.applyTo(*effective, !profile.enabled);
    request.uriOverrides.applyTo(*effective);
    return effective;
}

int StreamLaunchRequest::findAppIndex(const QVector<NvApp>& apps) const
{
    if (hasAppId()) {
        for (int i = 0; i < apps.size(); ++i) {
            if (apps[i].id == appId) {
                return i;
            }
        }
    }
    if (!appName.isEmpty()) {
        for (int i = 0; i < apps.size(); ++i) {
            if (apps[i].name.compare(appName, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
    }
    return -1;
}

QString StreamLaunchRequest::appDescription() const
{
    if (hasAppId() && !appName.isEmpty()) {
        return QObject::tr("%1 (application ID %2)").arg(appName).arg(appId);
    }
    if (hasAppId()) {
        return QObject::tr("application ID %1").arg(appId);
    }
    return appName;
}
