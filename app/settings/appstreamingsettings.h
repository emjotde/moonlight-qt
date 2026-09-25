#pragma once

#include <QSet>
#include <QSettings>

class StreamingPreferences;

struct AppStreamingOverride
{
    bool enabled = false;
    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrateKbps = 0; // Zero inherits the global bitrate.
    int windowMode = -1; // Negative values inherit global preferences.
    int captureSysKeysMode = -1;
    QString preferredDisplay;
};

class AppStreamingSettings
{
public:
    static QString validate(const AppStreamingOverride& profile);
    static AppStreamingOverride load(QSettings& settings, const QString& hostUuid, int appId,
                                     QString* error = nullptr);
    static QString save(QSettings& settings, const QString& hostUuid, int appId,
                        const AppStreamingOverride& profile);
    static QString remove(QSettings& settings, const QString& hostUuid, int appId);

    static StreamingPreferences* resolve(const StreamingPreferences& global,
                                         const AppStreamingOverride& profile,
                                         const StreamingPreferences* cli = nullptr,
                                         const QSet<QString>& explicitOptions = {});

private:
    static QString key(const QString& hostUuid, int appId);
};
