#include "appstreamingsettings.h"
#include "streamingpreferences.h"

#include <QtDebug>

QString AppStreamingSettings::key(const QString& hostUuid, int appId)
{
    return QStringLiteral("appstreamingprofiles/%1/%2")
        .arg(QString::fromLatin1(hostUuid.toUtf8().toHex()))
        .arg(appId);
}

QString AppStreamingSettings::validate(const AppStreamingOverride& profile)
{
    if (!profile.enabled) {
        return QString();
    }

    if (profile.width < 256 || profile.width > 8192 ||
            profile.height < 256 || profile.height > 8192) {
        return QObject::tr("Width and height must each be between 256 and 8192 pixels.");
    }
    if (profile.fps < 10 || profile.fps > 9999) {
        return QObject::tr("Frame rate must be between 10 and 9999 FPS.");
    }
    if (profile.bitrateKbps != 0 && (profile.bitrateKbps < 500 || profile.bitrateKbps > 500000)) {
        return QObject::tr("Custom bitrate must be between 500 and 500000 Kbps.");
    }
    if (profile.windowMode < -1 || profile.windowMode > StreamingPreferences::WM_WINDOWED) {
        return QObject::tr("Select a valid launch mode.");
    }
    if (profile.captureSysKeysMode < -1 || profile.captureSysKeysMode > StreamingPreferences::CSK_ALWAYS) {
        return QObject::tr("Select a valid system shortcut mode.");
    }
    if (profile.preferredDisplay.size() > 1024) {
        return QObject::tr("The saved monitor identifier is invalid.");
    }
    return QString();
}

AppStreamingOverride AppStreamingSettings::load(QSettings& settings, const QString& hostUuid,
                                               int appId, QString* error)
{
    AppStreamingOverride profile;
    QString message;
    if (error) {
        error->clear();
    }

    if (hostUuid.isEmpty() || appId <= 0) {
        message = QObject::tr("Cannot read stream settings without a host ID and application ID.");
    }
    else {
        settings.beginGroup(key(hostUuid, appId));
        profile.enabled = settings.value("enabled", false).toBool();
        if (profile.enabled) {
            bool widthOk, heightOk, fpsOk, bitrateOk, windowModeOk, captureModeOk;
            profile.width = settings.value("width").toInt(&widthOk);
            profile.height = settings.value("height").toInt(&heightOk);
            profile.fps = settings.value("fps").toInt(&fpsOk);
            profile.bitrateKbps = settings.value("bitrate", 0).toInt(&bitrateOk);
            profile.windowMode = settings.value("windowmode", -1).toInt(&windowModeOk);
            profile.captureSysKeysMode = settings.value("capturesyskeys", -1).toInt(&captureModeOk);
            profile.preferredDisplay = settings.value("display").toString();
            message = widthOk && heightOk && fpsOk && bitrateOk && windowModeOk && captureModeOk ?
                          validate(profile) : QObject::tr("Saved stream settings contain incomplete or invalid values.");
        }
        settings.endGroup();
        if (settings.status() != QSettings::NoError) {
            message = QObject::tr("Unable to read saved stream settings.");
        }
    }

    if (!message.isEmpty()) {
        qWarning() << "Ignoring application stream settings:" << message;
        if (error) {
            *error = message;
        }
        return AppStreamingOverride();
    }
    return profile;
}

QString AppStreamingSettings::save(QSettings& settings, const QString& hostUuid, int appId,
                                  const AppStreamingOverride& profile)
{
    if (!profile.enabled) {
        return remove(settings, hostUuid, appId);
    }

    QString error = validate(profile);
    if (hostUuid.isEmpty() || appId <= 0) {
        error = QObject::tr("Cannot save stream settings without a host ID and application ID.");
    }
    if (!error.isEmpty()) {
        qWarning() << "Cannot save application stream settings:" << error;
        return error;
    }

    settings.beginGroup(key(hostUuid, appId));
    settings.setValue("enabled", true);
    settings.setValue("width", profile.width);
    settings.setValue("height", profile.height);
    settings.setValue("fps", profile.fps);
    if (profile.windowMode >= 0) {
        settings.setValue("windowmode", profile.windowMode);
    }
    else {
        settings.remove("windowmode");
    }
    if (profile.captureSysKeysMode >= 0) {
        settings.setValue("capturesyskeys", profile.captureSysKeysMode);
    }
    else {
        settings.remove("capturesyskeys");
    }
    if (!profile.preferredDisplay.isEmpty()) {
        settings.setValue("display", profile.preferredDisplay);
    }
    else {
        settings.remove("display");
    }
    if (profile.bitrateKbps != 0) {
        settings.setValue("bitrate", profile.bitrateKbps);
    }
    else {
        settings.remove("bitrate");
    }
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        error = QObject::tr("Unable to save stream settings. Check that your configuration is writable.");
        qWarning() << error;
    }
    return error;
}

QString AppStreamingSettings::remove(QSettings& settings, const QString& hostUuid, int appId)
{
    QString error;
    if (hostUuid.isEmpty() || appId <= 0) {
        error = QObject::tr("Cannot remove stream settings without a host ID and application ID.");
    }
    else {
        settings.remove(key(hostUuid, appId));
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            error = QObject::tr("Unable to remove stream settings. Check that your configuration is writable.");
        }
    }
    if (!error.isEmpty()) {
        qWarning() << error;
    }
    return error;
}
