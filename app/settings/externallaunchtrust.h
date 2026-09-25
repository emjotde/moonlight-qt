#pragma once

#include <QSettings>
#include <QString>

class ExternalLaunchTrust
{
public:
    static bool shouldConfirm(bool confirmationEnabled, bool hostPaired, bool hostTrusted);
    static bool isTrusted(QSettings& settings, const QString& hostUuid);
    static QString trustHost(QSettings& settings, const QString& hostUuid);
    static QString removeHostTrust(QSettings& settings, const QString& hostUuid);

private:
    static QString key(const QString& hostUuid);
};
