#include "urischemeregistrar.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
QString normalizedExecutablePath(const QString& executablePath)
{
    return QDir::toNativeSeparators(QFileInfo(executablePath).absoluteFilePath());
}

#ifdef Q_OS_WIN
QString registryPath(const QString& scheme)
{
    return QStringLiteral("Software\\Classes\\%1").arg(scheme);
}

bool setRegistryString(HKEY root, const QString& subkey, const wchar_t* valueName,
                       const QString& value, QString& error)
{
    HKEY key;
    const LONG createResult = RegCreateKeyExW(
        root, reinterpret_cast<const wchar_t*>(subkey.utf16()), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (createResult != ERROR_SUCCESS) {
        error = QObject::tr("Unable to create the URI registration key (error %1).")
                    .arg(createResult);
        return false;
    }
    const DWORD byteCount =
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG setResult = RegSetValueExW(
        key, valueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.utf16()), byteCount);
    RegCloseKey(key);
    if (setResult != ERROR_SUCCESS) {
        error = QObject::tr("Unable to write the URI registration (error %1).")
                    .arg(setResult);
        return false;
    }
    return true;
}

bool queryRegistryString(HKEY root, const QString& subkey, const wchar_t* valueName,
                         QString& value, bool& present, QString& error)
{
    HKEY key;
    const LONG openResult = RegOpenKeyExW(
        root, reinterpret_cast<const wchar_t*>(subkey.utf16()), 0, KEY_QUERY_VALUE, &key);
    if (openResult == ERROR_FILE_NOT_FOUND) {
        present = false;
        return true;
    }
    if (openResult != ERROR_SUCCESS) {
        error = QObject::tr("Unable to read the URI registration key (error %1).")
                    .arg(openResult);
        return false;
    }

    DWORD type;
    DWORD byteCount = 0;
    LONG result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount);
    if (result == ERROR_FILE_NOT_FOUND) {
        present = false;
        RegCloseKey(key);
        return true;
    }
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        error = QObject::tr("Unable to read a URI registration value (error %1).")
                    .arg(result);
        RegCloseKey(key);
        return false;
    }

    QVector<wchar_t> buffer((byteCount / sizeof(wchar_t)) + 1);
    result = RegQueryValueExW(key, valueName, nullptr, &type,
                              reinterpret_cast<BYTE*>(buffer.data()), &byteCount);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) {
        error = QObject::tr("Unable to read a URI registration value (error %1).")
                    .arg(result);
        return false;
    }
    buffer.last() = L'\0';
    value = QString::fromWCharArray(buffer.constData());
    present = true;
    return true;
}
#endif
}

bool UriSchemeRegistrar::validateScheme(const QString& scheme, QString& error)
{
    if (!QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]{0,63}$"))
             .match(scheme)
             .hasMatch()) {
        error = QObject::tr("The URI scheme name is invalid.");
        return false;
    }
    return true;
}

QString UriSchemeRegistrar::commandForExecutable(const QString& executablePath)
{
    const QString path = normalizedExecutablePath(executablePath);
    return QStringLiteral("\"") + path + QStringLiteral("\" --uri \"%1\"");
}

QString UriSchemeRegistrar::iconForExecutable(const QString& executablePath)
{
    return QStringLiteral("\"") + normalizedExecutablePath(executablePath) +
           QStringLiteral("\",0");
}

bool UriSchemeRegistrar::registerScheme(const QString& executablePath, QString& error,
                                        const QString& scheme)
{
    error.clear();
    if (!validateScheme(scheme, error)) {
        return false;
    }
    const QString path = normalizedExecutablePath(executablePath);
    if (path.isEmpty() || path.contains('"')) {
        error = QObject::tr("The Moonlight executable path is invalid.");
        return false;
    }

#ifdef Q_OS_WIN
    const QString root = registryPath(scheme);
    if (!setRegistryString(HKEY_CURRENT_USER, root, nullptr,
                           QObject::tr("URL:Moonlight Protocol"), error) ||
            !setRegistryString(HKEY_CURRENT_USER, root, L"URL Protocol", QString(), error) ||
            !setRegistryString(HKEY_CURRENT_USER, root + QStringLiteral("\\DefaultIcon"),
                               nullptr, iconForExecutable(path), error) ||
            !setRegistryString(HKEY_CURRENT_USER,
                               root + QStringLiteral("\\shell\\open\\command"),
                               nullptr, commandForExecutable(path), error)) {
        return false;
    }
    return true;
#else
    Q_UNUSED(path);
    error = QObject::tr("URI registration is only supported on Windows.");
    return false;
#endif
}

UriSchemeRegistration UriSchemeRegistrar::readRegistration(QString& error,
                                                           const QString& scheme)
{
    UriSchemeRegistration registration;
    error.clear();
    if (!validateScheme(scheme, error)) {
        return registration;
    }

#ifdef Q_OS_WIN
    const QString root = registryPath(scheme);
    bool descriptionPresent = false;
    bool iconPresent = false;
    bool commandPresent = false;
    if (!queryRegistryString(HKEY_CURRENT_USER, root, nullptr,
                             registration.description, descriptionPresent, error) ||
            !queryRegistryString(HKEY_CURRENT_USER, root, L"URL Protocol",
                                 registration.command, registration.urlProtocolValuePresent,
                                 error) ||
            !queryRegistryString(HKEY_CURRENT_USER, root + QStringLiteral("\\DefaultIcon"),
                                 nullptr, registration.icon, iconPresent, error) ||
            !queryRegistryString(HKEY_CURRENT_USER,
                                 root + QStringLiteral("\\shell\\open\\command"),
                                 nullptr, registration.command, commandPresent, error)) {
        return UriSchemeRegistration();
    }
    registration.exists =
        descriptionPresent || registration.urlProtocolValuePresent || iconPresent || commandPresent;
#else
    error = QObject::tr("URI registration is only supported on Windows.");
#endif
    return registration;
}

bool UriSchemeRegistrar::unregisterScheme(const QString& executablePath, QString& error,
                                          const QString& scheme)
{
    error.clear();
    if (!validateScheme(scheme, error)) {
        return false;
    }

#ifdef Q_OS_WIN
    const auto registration = readRegistration(error, scheme);
    if (!error.isEmpty()) {
        return false;
    }
    if (!registration.exists) {
        return true;
    }
    const QString expected = commandForExecutable(executablePath);
    if (registration.command != expected) {
        error = QObject::tr("The %1 URI scheme is registered to a different application.")
                    .arg(scheme);
        return false;
    }
    const LONG result = RegDeleteTreeW(
        HKEY_CURRENT_USER,
        reinterpret_cast<const wchar_t*>(registryPath(scheme).utf16()));
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
        error = QObject::tr("Unable to remove the URI registration (error %1).")
                    .arg(result);
        return false;
    }
    return true;
#else
    Q_UNUSED(executablePath);
    error = QObject::tr("URI registration is only supported on Windows.");
    return false;
#endif
}
