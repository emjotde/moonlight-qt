#include "urilaunchrequest.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <limits>

namespace {
constexpr int MaxUriLength = 4096;
constexpr int MaxHostLength = 255;
constexpr int MaxAppNameLength = 512;
constexpr int MaxScalarLength = 64;

bool containsControlCharacter(const QString& value)
{
    for (const QChar character : value) {
        if (character.unicode() < 0x20 || character.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

bool isHexDigit(QChar character)
{
    const ushort value = character.unicode();
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

int hexValue(QChar character)
{
    if (character >= '0' && character <= '9') {
        return character.unicode() - '0';
    }
    return character.toLower().unicode() - 'a' + 10;
}

QString normalizedValue(const QString& value)
{
    return value.toLower();
}
}

bool UriLaunchRequestParser::decodeComponent(const QString& encoded,
                                             QString& decoded,
                                             QString& error)
{
    QByteArray bytes;
    QString literal;
    for (int i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '%') {
            literal.append(encoded[i]);
            continue;
        }

        bytes.append(literal.toUtf8());
        literal.clear();
        if (i + 2 >= encoded.size() ||
                !isHexDigit(encoded[i + 1]) ||
                !isHexDigit(encoded[i + 2])) {
            error = QObject::tr("The URI contains malformed percent encoding.");
            return false;
        }
        bytes.append(char((hexValue(encoded[i + 1]) << 4) | hexValue(encoded[i + 2])));
        i += 2;
    }
    bytes.append(literal.toUtf8());

    decoded = QString::fromUtf8(bytes.constData(), bytes.size());
    if (decoded.toUtf8() != bytes) {
        error = QObject::tr("The URI contains invalid UTF-8 text.");
        return false;
    }
    if (decoded.contains(QChar('\0')) || containsControlCharacter(decoded)) {
        error = QObject::tr("The URI contains a prohibited control character.");
        return false;
    }
    return true;
}

bool UriLaunchRequestParser::parseInteger(const QString& name,
                                          const QString& value,
                                          int minimum,
                                          int maximum,
                                          int& result,
                                          QString& error)
{
    if (value.isEmpty() || value.size() > MaxScalarLength ||
            !QRegularExpression(QStringLiteral("^\\d+$")).match(value).hasMatch()) {
        error = QObject::tr("Invalid %1 value: %2").arg(name, value);
        return false;
    }
    bool ok;
    const qlonglong parsed = value.toLongLong(&ok);
    if (!ok || parsed < minimum || parsed > maximum) {
        error = QObject::tr("%1 must be between %2 and %3.").arg(name).arg(minimum).arg(maximum);
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

UriLaunchParseResult UriLaunchRequestParser::parse(
    const QString& uri,
    const StreamingPreferences& globalPreferences)
{
    UriLaunchParseResult result;
    result.request.source = StreamLaunchRequest::UriSource;
    if (uri.isEmpty() || uri.size() > MaxUriLength) {
        result.error = QObject::tr("The Moonlight URI is empty or exceeds %1 characters.")
                           .arg(MaxUriLength);
        return result;
    }
    if (uri.contains(QChar('\0')) || containsControlCharacter(uri)) {
        result.error = QObject::tr("The Moonlight URI contains a prohibited control character.");
        return result;
    }
    if (uri.contains('#')) {
        result.error = QObject::tr("URI fragments are not supported.");
        return result;
    }

    const QString prefix = QStringLiteral("moonlight://");
    if (!uri.startsWith(prefix, Qt::CaseInsensitive)) {
        result.error = QObject::tr("The URI scheme must be moonlight://.");
        return result;
    }
    const int queryStart = uri.indexOf('?');
    QString action = uri.mid(prefix.size(),
                             queryStart < 0 ? -1 : queryStart - prefix.size());
    if (action.endsWith('/')) {
        action.chop(1);
    }
    if (action.compare(QStringLiteral("stream"), Qt::CaseInsensitive) != 0) {
        result.error = QObject::tr("Unknown Moonlight URI action: %1").arg(action);
        return result;
    }

    QMap<QString, QString> parameters;
    const QString query = queryStart < 0 ? QString() : uri.mid(queryStart + 1);
    if (!query.isEmpty()) {
        const QStringList pairs = query.split('&', Qt::KeepEmptyParts);
        for (const QString& pair : pairs) {
            if (pair.isEmpty()) {
                result.error = QObject::tr("The URI contains an empty query parameter.");
                return result;
            }
            const int separator = pair.indexOf('=');
            if (separator <= 0) {
                result.error = QObject::tr("Every URI query parameter must have a value.");
                return result;
            }

            QString name;
            QString value;
            if (!decodeComponent(pair.left(separator), name, result.error) ||
                    !decodeComponent(pair.mid(separator + 1), value, result.error)) {
                return result;
            }
            if (parameters.contains(name)) {
                result.error = QObject::tr("Duplicate URI parameter: %1").arg(name);
                return result;
            }
            parameters.insert(name, value);
        }
    }

    const QSet<QString> allowedParameters = {
        QStringLiteral("host"),
        QStringLiteral("app"),
        QStringLiteral("appId"),
        QStringLiteral("resolution"),
        QStringLiteral("fps"),
        QStringLiteral("bitrate"),
        QStringLiteral("displayMode"),
        QStringLiteral("codec"),
        QStringLiteral("audioConfig"),
        QStringLiteral("quitAfter"),
    };
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        if (!allowedParameters.contains(it.key())) {
            result.error = QObject::tr("Unknown URI parameter: %1").arg(it.key());
            return result;
        }
    }

    result.request.host = parameters.value(QStringLiteral("host"));
    if (result.request.host.isEmpty()) {
        result.error = QObject::tr("The URI is missing the required host parameter.");
        return result;
    }
    if (result.request.host.size() > MaxHostLength ||
            result.request.host.contains(QRegularExpression(QStringLiteral("\\s")))) {
        result.error = QObject::tr("The host parameter is too long or contains whitespace.");
        return result;
    }

    result.request.appName = parameters.value(QStringLiteral("app"));
    if (result.request.appName.size() > MaxAppNameLength) {
        result.error = QObject::tr("The application name exceeds %1 characters.").arg(MaxAppNameLength);
        return result;
    }
    if (parameters.contains(QStringLiteral("appId")) &&
            !parseInteger(QObject::tr("application ID"),
                          parameters.value(QStringLiteral("appId")),
                          1, std::numeric_limits<int>::max(),
                          result.request.appId, result.error)) {
        return result;
    }
    if (!result.request.hasAppId() && result.request.appName.isEmpty()) {
        result.error = QObject::tr("The URI must include app or appId.");
        return result;
    }

    result.request.uriOverrides = StreamLaunchOverrides(globalPreferences);
    auto preferences = result.request.uriOverrides.values();
    if (parameters.contains(QStringLiteral("resolution"))) {
        const auto match = QRegularExpression(QStringLiteral("^(\\d{1,5})[xX](\\d{1,5})$"))
                               .match(parameters.value(QStringLiteral("resolution")));
        if (!match.hasMatch()) {
            result.error = QObject::tr("Resolution must use the WIDTHxHEIGHT format.");
            return result;
        }
        bool widthOk;
        bool heightOk;
        preferences->width = match.captured(1).toInt(&widthOk);
        preferences->height = match.captured(2).toInt(&heightOk);
        if (!widthOk || !heightOk ||
                preferences->width < 256 || preferences->width > 8192 ||
                preferences->height < 256 || preferences->height > 8192) {
            result.error = QObject::tr("Resolution width and height must each be between 256 and 8192.");
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::Resolution);
    }
    if (parameters.contains(QStringLiteral("fps"))) {
        if (!parseInteger(QObject::tr("FPS"), parameters.value(QStringLiteral("fps")),
                          10, 480, preferences->fps, result.error)) {
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::Fps);
    }
    if (parameters.contains(QStringLiteral("bitrate"))) {
        if (!parseInteger(QObject::tr("Bitrate"), parameters.value(QStringLiteral("bitrate")),
                          500, 500000, preferences->bitrateKbps, result.error)) {
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::Bitrate);
    }
    if (parameters.contains(QStringLiteral("displayMode"))) {
        const QString mode = normalizedValue(parameters.value(QStringLiteral("displayMode")));
        if (mode == QStringLiteral("fullscreen")) {
            preferences->windowMode = StreamingPreferences::WM_FULLSCREEN;
        }
        else if (mode == QStringLiteral("borderless")) {
            preferences->windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
        }
        else if (mode == QStringLiteral("windowed")) {
            preferences->windowMode = StreamingPreferences::WM_WINDOWED;
        }
        else {
            result.error = QObject::tr("Invalid displayMode value.");
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::WindowMode);
    }
    if (parameters.contains(QStringLiteral("codec"))) {
        const QString codec = normalizedValue(parameters.value(QStringLiteral("codec")));
        if (codec == QStringLiteral("auto")) {
            preferences->videoCodecConfig = StreamingPreferences::VCC_AUTO;
        }
        else if (codec == QStringLiteral("h.264")) {
            preferences->videoCodecConfig = StreamingPreferences::VCC_FORCE_H264;
        }
        else if (codec == QStringLiteral("hevc")) {
            preferences->videoCodecConfig = StreamingPreferences::VCC_FORCE_HEVC;
        }
        else if (codec == QStringLiteral("av1")) {
            preferences->videoCodecConfig = StreamingPreferences::VCC_FORCE_AV1;
        }
        else {
            result.error = QObject::tr("Invalid codec value.");
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::VideoCodec);
    }
    if (parameters.contains(QStringLiteral("audioConfig"))) {
        const QString audio = normalizedValue(parameters.value(QStringLiteral("audioConfig")));
        if (audio == QStringLiteral("stereo")) {
            preferences->audioConfig = StreamingPreferences::AC_STEREO;
        }
        else if (audio == QStringLiteral("5.1-surround")) {
            preferences->audioConfig = StreamingPreferences::AC_51_SURROUND;
        }
        else if (audio == QStringLiteral("7.1-surround")) {
            preferences->audioConfig = StreamingPreferences::AC_71_SURROUND;
        }
        else {
            result.error = QObject::tr("Invalid audioConfig value.");
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::AudioConfig);
    }
    if (parameters.contains(QStringLiteral("quitAfter"))) {
        const QString boolean = normalizedValue(parameters.value(QStringLiteral("quitAfter")));
        if (boolean == QStringLiteral("true")) {
            preferences->quitAppAfter = true;
        }
        else if (boolean == QStringLiteral("false")) {
            preferences->quitAppAfter = false;
        }
        else {
            result.error = QObject::tr("quitAfter must be true or false.");
            return result;
        }
        result.request.uriOverrides.markExplicit(StreamLaunchOverrides::QuitAfter);
    }

    return result;
}
