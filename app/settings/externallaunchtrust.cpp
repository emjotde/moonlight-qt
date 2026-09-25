#include "externallaunchtrust.h"

#include <QtDebug>

QString ExternalLaunchTrust::key(const QString& hostUuid)
{
    return QStringLiteral("externallaunch/trustedhosts/%1")
        .arg(QString::fromLatin1(hostUuid.toUtf8().toHex()));
}

bool ExternalLaunchTrust::shouldConfirm(bool confirmationEnabled,
                                        bool hostPaired,
                                        bool hostTrusted)
{
    return confirmationEnabled && hostPaired && !hostTrusted;
}

bool ExternalLaunchTrust::isTrusted(QSettings& settings, const QString& hostUuid)
{
    if (hostUuid.isEmpty()) {
        return false;
    }
    return settings.value(key(hostUuid), false).toBool();
}

QString ExternalLaunchTrust::trustHost(QSettings& settings, const QString& hostUuid)
{
    if (hostUuid.isEmpty()) {
        return QObject::tr("Cannot trust an external launch without a stable host ID.");
    }
    settings.setValue(key(hostUuid), true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        const QString error =
            QObject::tr("Unable to save external-launch trust for this host.");
        qWarning() << error;
        return error;
    }
    return QString();
}

QString ExternalLaunchTrust::removeHostTrust(QSettings& settings, const QString& hostUuid)
{
    if (hostUuid.isEmpty()) {
        return QObject::tr("Cannot remove external-launch trust without a stable host ID.");
    }
    settings.remove(key(hostUuid));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        const QString error =
            QObject::tr("Unable to remove external-launch trust for this host.");
        qWarning() << error;
        return error;
    }
    return QString();
}
