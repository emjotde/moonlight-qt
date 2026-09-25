#pragma once

#include <QString>

struct UriSchemeRegistration
{
    bool exists = false;
    bool urlProtocolValuePresent = false;
    QString description;
    QString icon;
    QString command;
};

class UriSchemeRegistrar
{
public:
    static bool registerScheme(const QString& executablePath, QString& error,
                               const QString& scheme = QStringLiteral("moonlight"));
    static bool unregisterScheme(const QString& executablePath, QString& error,
                                 const QString& scheme = QStringLiteral("moonlight"));
    static UriSchemeRegistration readRegistration(
        QString& error, const QString& scheme = QStringLiteral("moonlight"));

    static QString commandForExecutable(const QString& executablePath);
    static QString iconForExecutable(const QString& executablePath);

private:
    static bool validateScheme(const QString& scheme, QString& error);
};
