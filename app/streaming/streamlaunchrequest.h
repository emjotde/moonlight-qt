#pragma once

#include "settings/appstreamingsettings.h"
#include "settings/streamingpreferences.h"

#include <QSharedPointer>
#include <QString>
#include <QVector>

class NvApp;

class StreamLaunchOverrides
{
public:
    enum Field
    {
        Resolution,
        Fps,
        Bitrate,
        PacketSize,
        WindowMode,
        Vsync,
        AudioConfig,
        MultiController,
        QuitAfter,
        AbsoluteMouse,
        SwapMouseButtons,
        AbsoluteTouch,
        GameOptimizations,
        PlayAudioOnHost,
        FramePacing,
        MuteOnFocusLoss,
        BackgroundGamepad,
        ReverseScroll,
        SwapFaceButtons,
        KeepAwake,
        PerformanceOverlay,
        Hdr,
        Yuv444,
        CaptureSystemKeys,
        VideoCodec,
        VideoDecoder,
        FieldCount,
    };

    StreamLaunchOverrides() = default;
    explicit StreamLaunchOverrides(const StreamingPreferences& base);

    StreamingPreferences* values();
    const StreamingPreferences* values() const;

    void markExplicit(Field field);
    void markDerived(Field field);
    bool isExplicit(Field field) const;
    bool isDerived(Field field) const;
    bool isEmpty() const;

    void applyTo(StreamingPreferences& target, bool includeDerived = false) const;

private:
    static quint64 fieldMask(Field field);
    void applyField(StreamingPreferences& target, Field field) const;

    QSharedPointer<StreamingPreferences> m_Values;
    quint64 m_ExplicitFields = 0;
    quint64 m_DerivedFields = 0;
};

struct StreamLaunchRequest
{
    enum Source
    {
        GuiSource,
        CliSource,
        UriSource,
    };

    QString host;
    QString appName;
    int appId = 0;
    Source source = GuiSource;
    StreamLaunchOverrides cliOverrides;
    StreamLaunchOverrides uriOverrides;

    bool hasAppId() const { return appId > 0; }
    int findAppIndex(const QVector<NvApp>& apps) const;
    QString appDescription() const;
};

class StreamLaunchPreferences
{
public:
    static StreamingPreferences* resolve(const StreamingPreferences& global,
                                         const AppStreamingOverride& profile,
                                         const StreamLaunchRequest& request);

private:
    static void applyProfile(StreamingPreferences& target,
                             const StreamingPreferences& global,
                             const AppStreamingOverride& profile);
};
