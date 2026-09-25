#include "commandlineparser.h"

#include <QCommandLineParser>
#include <QRegularExpression>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#endif

static bool inRange(int value, int min, int max)
{
    return value >= min && value <= max;
}

// This method returns key's value from QMap where the key is a QString.
// Key matching is case insensitive.
template <typename T>
static T mapValue(QMap<QString, T> map, QString key)
{
    for(auto& item : map.toStdMap()) {
        if (QString::compare(item.first, key, Qt::CaseInsensitive) == 0) {
            return item.second;
        }
    }
    return T();
}

class CommandLineParser : public QCommandLineParser
{
public:
    enum MessageType {
        Info,
        Error
    };

    void setupCommonOptions()
    {
        setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
        addHelpOption();
        addVersionOption();
    }

    void handleHelpAndVersionOptions()
    {
        if (isSet("help")) {
            showInfo(helpText());
        }
        if (isSet("version")) {
            showVersion();
        }
    }

    void handleUnknownOptions()
    {
        if (!unknownOptionNames().isEmpty()) {
            showError(QString("Unknown options: %1").arg(unknownOptionNames().join(", ")));
        }
    }

    void showMessage(QString message, MessageType type) const
    {
    #if defined(Q_OS_WIN32)
        UINT flags = MB_OK | MB_TOPMOST | MB_SETFOREGROUND;
        flags |= (type == Info ? MB_ICONINFORMATION : MB_ICONERROR);
        QString title = "Moonlight";
        MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(message.utf16()),
                    reinterpret_cast<const wchar_t *>(title.utf16()), flags);
    #endif
        message = message.endsWith('\n') ? message : message + '\n';
        fputs(qPrintable(message), type == Info ? stdout : stderr);
    }

    [[ noreturn ]] void showInfo(QString message) const
    {
        showMessage(message, Info);
        exit(0);
    }

    [[ noreturn ]] void showError(QString message) const
    {
        showMessage(message + "\n\n" + helpText(), Error);
        exit(1);
    }

    int getIntOption(QString name) const
    {
        bool ok;
        int intValue = value(name).toInt(&ok);
        if (!ok) {
            showError(QString("Invalid %1 value: %2").arg(name, value(name)));
        }
        return intValue;
    }

    bool getToggleOptionValue(QString name, bool defaultValue) const
    {
        QRegularExpression re(QString("^(%1|no-%1)$").arg(name));
        QStringList options = optionNames().filter(re);
        if (options.isEmpty()) {
            return defaultValue;
        } else {
            return options.last() == name;
        }
    }

    QString getChoiceOptionValue(QString name) const
    {
        if (!m_Choices[name].contains(value(name), Qt::CaseInsensitive)) {
            showError(QString("Invalid %1 choice: %2").arg(name, value(name)));
        }
        return value(name);
    }

    QPair<int,int> getResolutionOptionValue(QString name) const
    {
        QRegularExpression re("^(\\d+)x(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        auto match = re.match(value(name));
        if (!match.hasMatch()) {
            showError(QString("Invalid %1 format: %2").arg(name, value(name)));
        }
        return qMakePair(match.captured(1).toInt(), match.captured(2).toInt());
    }

    void addFlagOption(QString name, QString descriptiveName)
    {
        addOption(QCommandLineOption(name, QString("Use %1.").arg(descriptiveName)));
    }

    void addToggleOption(QString name, QString descriptiveName)
    {
        addOption(QCommandLineOption(name, QString("Use %1.").arg(descriptiveName)));
        addOption(QCommandLineOption("no-" + name, QString("Do not use %1.").arg(descriptiveName)));
    }

    void addValueOption(QString name, QString descriptiveName)
    {
        addOption(QCommandLineOption(name, QString("Specify %1 to use.").arg(descriptiveName), name));
    }

    void addChoiceOption(QString name, QString descriptiveName, QStringList choices)
    {
        addOption(QCommandLineOption(name, QString("Select %1: %2.").arg(descriptiveName, choices.join('/')), name));
        m_Choices[name] = choices;
    }

private:
    QMap<QString, QStringList> m_Choices;
};

GlobalCommandLineParser::GlobalCommandLineParser()
{
}

GlobalCommandLineParser::~GlobalCommandLineParser()
{
}

GlobalCommandLineParser::ParseResult GlobalCommandLineParser::parse(const QStringList &args)
{
    CommandLineParser parser;
    parser.setupCommonOptions();
    parser.setApplicationDescription(
        "\n"
        "Starts Moonlight normally if no arguments are given.\n"
        "\n"
        "Available actions:\n"
        "  list            List the available apps on a host\n"
        "  quit            Quit the currently running app\n"
        "  stream          Start streaming an app\n"
        "  pair            Pair a new host\n"
        "\n"
        "Other options:\n"
        "  --uri <URI>     Process a moonlight:// launch URI\n"
        "  --register-uri  Register moonlight:// for the current user\n"
        "  --unregister-uri Remove this executable's moonlight:// registration\n"
        "\n"
        "See 'moonlight <action> --help' for help of specific action."
    );
    parser.addPositionalArgument("action", "Action to execute", "<action>");
    parser.addValueOption("uri", "moonlight:// launch URI");
    parser.addFlagOption("register-uri", "per-user moonlight:// protocol registration");
    parser.addFlagOption("unregister-uri", "remove this executable's moonlight:// registration");
    if (!parser.parse(args)) {
        parser.showError(parser.errorText());
    }
    auto posArgs = parser.positionalArguments();

    if (parser.isSet("register-uri") || parser.isSet("unregister-uri")) {
        parser.handleUnknownOptions();
        if (parser.isSet("register-uri") && parser.isSet("unregister-uri")) {
            parser.showError("--register-uri and --unregister-uri cannot be combined");
        }
        if (parser.isSet("uri") || !posArgs.isEmpty()) {
            parser.showError("URI registration cannot be combined with another action");
        }
        return parser.isSet("register-uri") ? RegisterUriRequested : UnregisterUriRequested;
    }

    if (parser.isSet("uri")) {
        parser.handleUnknownOptions();
        if (parser.values("uri").size() != 1) {
            parser.showError("--uri may only be specified once");
        }
        if (!posArgs.isEmpty()) {
            parser.showError("--uri cannot be combined with another action");
        }
        m_Uri = parser.value("uri");
        return UriRequested;
    }

    if (posArgs.isEmpty()) {
        // This method will not return and terminates the process if --version
        // or --help is specified
        parser.handleHelpAndVersionOptions();
        parser.handleUnknownOptions();
        return NormalStartRequested;
    }
    else {
        // If users supply arguments that accept values prior to the "quit"
        // or "stream" positional arguments, we will not be able to correctly
        // parse the value out of the input because this QCommandLineParser
        // doesn't know about all of the options that "quit" and "stream"
        // commands can accept. To work around this issue, we just look
        // for "quit" or "stream" positional arguments anywhere.
        for (int i = 0; i < posArgs.size(); i++) {
            QString action = posArgs.at(i).toLower();
            if (action == "quit") {
                return QuitRequested;
            } else if (action == "stream") {
                return StreamRequested;
            } else if (action == "pair") {
                return PairRequested;
            } else if (action == "list") {
                return ListRequested;
            }
        }

        parser.showError(QString("Invalid action"));
    }
}

QString GlobalCommandLineParser::getUri() const
{
    return m_Uri;
}

QuitCommandLineParser::QuitCommandLineParser()
{
}

QuitCommandLineParser::~QuitCommandLineParser()
{
}

void QuitCommandLineParser::parse(const QStringList &args)
{
    CommandLineParser parser;
    parser.setupCommonOptions();
    parser.setApplicationDescription(
        "\n"
        "Quit the currently running app on the given host."
    );
    parser.addPositionalArgument("quit", "quit running app");
    parser.addPositionalArgument("host", "Host computer name, UUID, or IP address", "<host>");

    if (!parser.parse(args)) {
        parser.showError(parser.errorText());
    }

    parser.handleUnknownOptions();

    // This method will not return and terminates the process if --version or
    // --help is specified
    parser.handleHelpAndVersionOptions();

    // Verify that host has been provided
    auto posArgs = parser.positionalArguments();
    if (posArgs.length() < 2) {
        parser.showError("Host not provided");
    }
    m_Host = parser.positionalArguments().at(1);
}

QString QuitCommandLineParser::getHost() const
{
    return m_Host;
}

PairCommandLineParser::PairCommandLineParser()
{
}

PairCommandLineParser::~PairCommandLineParser()
{
}

void PairCommandLineParser::parse(const QStringList &args)
{
    CommandLineParser parser;
    parser.setupCommonOptions();
    parser.setApplicationDescription(
        "\n"
        "Pair with the specified host."
    );
    parser.addPositionalArgument("pair", "pair host");
    parser.addPositionalArgument("host", "Host computer name, UUID, or IP address", "<host>");
    parser.addValueOption("pin", "4 digit pairing PIN");

    if (!parser.parse(args)) {
        parser.showError(parser.errorText());
    }

    parser.handleUnknownOptions();

    // This method will not return and terminates the process if --version or
    // --help is specified
    parser.handleHelpAndVersionOptions();

    // Verify that host has been provided
    auto posArgs = parser.positionalArguments();
    if (posArgs.length() < 2) {
        parser.showError("Host not provided");
    }
    m_Host = parser.positionalArguments().at(1);
    m_PredefinedPin = parser.value("pin");
    if (!m_PredefinedPin.isEmpty() && m_PredefinedPin.length() != 4) {
        parser.showError("PIN must be 4 digits");
    }
}

QString PairCommandLineParser::getHost() const
{
    return m_Host;
}

QString PairCommandLineParser::getPredefinedPin() const
{
    return m_PredefinedPin;
}

StreamCommandLineParser::StreamCommandLineParser()
{
    m_WindowModeMap = {
        {"fullscreen", StreamingPreferences::WM_FULLSCREEN},
        {"windowed",   StreamingPreferences::WM_WINDOWED},
        {"borderless", StreamingPreferences::WM_FULLSCREEN_DESKTOP},
    };
    m_AudioConfigMap = {
        {"stereo",       StreamingPreferences::AC_STEREO},
        {"5.1-surround", StreamingPreferences::AC_51_SURROUND},
        {"7.1-surround", StreamingPreferences::AC_71_SURROUND},
    };
    m_VideoCodecMap = {
        {"auto",  StreamingPreferences::VCC_AUTO},
        {"H.264", StreamingPreferences::VCC_FORCE_H264},
        {"HEVC",  StreamingPreferences::VCC_FORCE_HEVC},
        {"AV1", StreamingPreferences::VCC_FORCE_AV1},
    };
    m_VideoDecoderMap = {
        {"auto",     StreamingPreferences::VDS_AUTO},
        {"software", StreamingPreferences::VDS_FORCE_SOFTWARE},
        {"hardware", StreamingPreferences::VDS_FORCE_HARDWARE},
    };
    m_CaptureSysKeysModeMap = {
        {"never",      StreamingPreferences::CSK_OFF},
        {"fullscreen", StreamingPreferences::CSK_FULLSCREEN},
        {"always",     StreamingPreferences::CSK_ALWAYS},
    };
}

StreamCommandLineParser::~StreamCommandLineParser()
{
}

StreamLaunchRequest StreamCommandLineParser::parse(const QStringList &args,
                                                   const StreamingPreferences& globalPreferences)
{
    StreamLaunchRequest request;
    request.source = StreamLaunchRequest::CliSource;
    request.cliOverrides = StreamLaunchOverrides(globalPreferences);
    auto preferences = request.cliOverrides.values();

    CommandLineParser parser;
    parser.setupCommonOptions();
    parser.setApplicationDescription(
        "\n"
        "Starts directly streaming a given app."
    );
    parser.addPositionalArgument("stream", "Start stream");

    // Add other arguments and options
    parser.addPositionalArgument("host", "Host computer name, UUID, or IP address", "<host>");
    parser.addPositionalArgument("app", "App to stream", "\"<app>\"");

    parser.addFlagOption("720",  "1280x720 resolution");
    parser.addFlagOption("1080", "1920x1080 resolution");
    parser.addFlagOption("1440", "2560x1440 resolution");
    parser.addFlagOption("4K", "3840x2160 resolution");
    parser.addValueOption("resolution", "custom <width>x<height> resolution");
    parser.addToggleOption("vsync", "V-Sync");
    parser.addValueOption("fps", "FPS");
    parser.addValueOption("bitrate", "bitrate in Kbps");
    parser.addValueOption("packet-size", "video packet size");
    parser.addChoiceOption("display-mode", "display mode", m_WindowModeMap.keys());
    parser.addChoiceOption("audio-config", "audio config", m_AudioConfigMap.keys());
    parser.addToggleOption("multi-controller", "multiple controller support");
    parser.addToggleOption("quit-after", "quit app after session");
    parser.addToggleOption("absolute-mouse", "remote desktop optimized mouse control");
    parser.addToggleOption("mouse-buttons-swap", "left and right mouse buttons swap");
    parser.addToggleOption("touchscreen-trackpad", "touchscreen in trackpad mode");
    parser.addToggleOption("game-optimization", "game optimizations");
    parser.addToggleOption("audio-on-host", "audio on host PC");
    parser.addToggleOption("frame-pacing", "frame pacing");
    parser.addToggleOption("mute-on-focus-loss", "mute audio when Moonlight window loses focus");
    parser.addToggleOption("background-gamepad", "background gamepad input");
    parser.addToggleOption("reverse-scroll-direction", "inverted scroll direction");
    parser.addToggleOption("swap-gamepad-buttons", "swap A/B and X/Y gamepad buttons (Nintendo-style)");
    parser.addToggleOption("keep-awake", "prevent display sleep while streaming");
    parser.addToggleOption("performance-overlay", "show performance overlay");
    parser.addToggleOption("hdr", "HDR streaming");
    parser.addToggleOption("yuv444", "YUV 4:4:4 sampling, if supported");
    parser.addChoiceOption("capture-system-keys", "capture system key combos", m_CaptureSysKeysModeMap.keys());
    parser.addChoiceOption("video-codec", "video codec", m_VideoCodecMap.keys());
    parser.addChoiceOption("video-decoder", "video decoder", m_VideoDecoderMap.keys());

    if (!parser.parse(args)) {
        parser.showError(parser.errorText());
    }

    parser.handleUnknownOptions();

    // Resolve display's width and height
    QRegularExpression resolutionRexExp("^(720|1080|1440|4K|resolution)$");
    QStringList resoOptions = parser.optionNames().filter(resolutionRexExp);
    bool displaySet = !resoOptions.isEmpty();
    if (displaySet) {
        QString name = resoOptions.last();
        if (name == "720") {
            preferences->width  = 1280;
            preferences->height = 720;
        } else if (name == "1080") {
            preferences->width  = 1920;
            preferences->height = 1080;
        } else if (name == "1440") {
            preferences->width  = 2560;
            preferences->height = 1440;
        } else if (name == "4K") {
            preferences->width  = 3840;
            preferences->height = 2160;
        } else if (name == "resolution") {
            auto resolution = parser.getResolutionOptionValue(name);
            preferences->width  = resolution.first;
            preferences->height = resolution.second;
        }
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Resolution);
    }

    // Resolve --fps option
    if (parser.isSet("fps")) {
        preferences->fps = parser.getIntOption("fps");
        if (!inRange(preferences->fps, 10, 480)) {
            fprintf(stderr, "Warning: FPS is out of the supported range (10 - 480 FPS). Performance may suffer!\n");
        }
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Fps);
    }

    // Resolve --bitrate option
    if (parser.isSet("bitrate")) {
        preferences->bitrateKbps = parser.getIntOption("bitrate");
        if (!inRange(preferences->bitrateKbps, 500, 500000)) {
            fprintf(stderr, "Warning: Bitrate is out of the supported range (500 - 500000 Kbps). Performance may suffer!\n");
        }
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Bitrate);
    } else if (displaySet || parser.isSet("fps")) {
        preferences->bitrateKbps = preferences->getDefaultBitrate(
            preferences->width, preferences->height, preferences->fps, preferences->enableYUV444);
        request.cliOverrides.markDerived(StreamLaunchOverrides::Bitrate);
    }

    // Resolve --packet-size option
    if (parser.isSet("packet-size")) {
        preferences->packetSize = parser.getIntOption("packet-size");
        if (preferences->packetSize < 1024) {
            parser.showError("Packet size must be greater than 1024 bytes");
        }
        request.cliOverrides.markExplicit(StreamLaunchOverrides::PacketSize);
    }

    // Resolve --display option
    if (parser.isSet("display-mode")) {
        preferences->windowMode = mapValue(m_WindowModeMap, parser.getChoiceOptionValue("display-mode"));
        request.cliOverrides.markExplicit(StreamLaunchOverrides::WindowMode);
    }

    // Resolve --vsync and --no-vsync options
    preferences->enableVsync = parser.getToggleOptionValue("vsync", preferences->enableVsync);
    if (parser.isSet("vsync") || parser.isSet("no-vsync")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Vsync);
    }

    // Resolve --audio-config option
    if (parser.isSet("audio-config")) {
        preferences->audioConfig = mapValue(m_AudioConfigMap, parser.getChoiceOptionValue("audio-config"));
        request.cliOverrides.markExplicit(StreamLaunchOverrides::AudioConfig);
    }

    // Resolve --multi-controller and --no-multi-controller options
    preferences->multiController = parser.getToggleOptionValue("multi-controller", preferences->multiController);
    if (parser.isSet("multi-controller") || parser.isSet("no-multi-controller")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::MultiController);
    }

    // Resolve --quit-after and --no-quit-after options
    preferences->quitAppAfter = parser.getToggleOptionValue("quit-after", preferences->quitAppAfter);
    if (parser.isSet("quit-after") || parser.isSet("no-quit-after")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::QuitAfter);
    }

    // Resolve --absolute-mouse and --no-absolute-mouse options
    preferences->absoluteMouseMode = parser.getToggleOptionValue("absolute-mouse", preferences->absoluteMouseMode);
    if (parser.isSet("absolute-mouse") || parser.isSet("no-absolute-mouse")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::AbsoluteMouse);
    }

    // Resolve --mouse-buttons-swap and --no-mouse-buttons-swap options
    preferences->swapMouseButtons = parser.getToggleOptionValue("mouse-buttons-swap", preferences->swapMouseButtons);
    if (parser.isSet("mouse-buttons-swap") || parser.isSet("no-mouse-buttons-swap")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::SwapMouseButtons);
    }

    // Resolve --touchscreen-trackpad and --no-touchscreen-trackpad options
    preferences->absoluteTouchMode = !parser.getToggleOptionValue("touchscreen-trackpad", !preferences->absoluteTouchMode);
    if (parser.isSet("touchscreen-trackpad") || parser.isSet("no-touchscreen-trackpad")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::AbsoluteTouch);
    }

    // Resolve --game-optimization and --no-game-optimization options
    preferences->gameOptimizations = parser.getToggleOptionValue("game-optimization", preferences->gameOptimizations);
    if (parser.isSet("game-optimization") || parser.isSet("no-game-optimization")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::GameOptimizations);
    }

    // Resolve --audio-on-host and --no-audio-on-host options
    preferences->playAudioOnHost = parser.getToggleOptionValue("audio-on-host", preferences->playAudioOnHost);
    if (parser.isSet("audio-on-host") || parser.isSet("no-audio-on-host")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::PlayAudioOnHost);
    }

    // Resolve --frame-pacing and --no-frame-pacing options
    preferences->framePacing = parser.getToggleOptionValue("frame-pacing", preferences->framePacing);
    if (parser.isSet("frame-pacing") || parser.isSet("no-frame-pacing")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::FramePacing);
    }

    // Resolve --mute-on-focus-loss and --no-mute-on-focus-loss options
    preferences->muteOnFocusLoss = parser.getToggleOptionValue("mute-on-focus-loss", preferences->muteOnFocusLoss);
    if (parser.isSet("mute-on-focus-loss") || parser.isSet("no-mute-on-focus-loss")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::MuteOnFocusLoss);
    }

    // Resolve --background-gamepad and --no-background-gamepad options
    preferences->backgroundGamepad = parser.getToggleOptionValue("background-gamepad", preferences->backgroundGamepad);
    if (parser.isSet("background-gamepad") || parser.isSet("no-background-gamepad")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::BackgroundGamepad);
    }

    // Resolve --reverse-scroll-direction and --no-reverse-scroll-direction options
    preferences->reverseScrollDirection = parser.getToggleOptionValue("reverse-scroll-direction", preferences->reverseScrollDirection);
    if (parser.isSet("reverse-scroll-direction") || parser.isSet("no-reverse-scroll-direction")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::ReverseScroll);
    }

    // Resolve --swap-gamepad-buttons and --no-swap-gamepad-buttons options
    preferences->swapFaceButtons = parser.getToggleOptionValue("swap-gamepad-buttons", preferences->swapFaceButtons);
    if (parser.isSet("swap-gamepad-buttons") || parser.isSet("no-swap-gamepad-buttons")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::SwapFaceButtons);
    }

    // Resolve --keep-awake and --no-keep-awake options
    preferences->keepAwake = parser.getToggleOptionValue("keep-awake", preferences->keepAwake);
    if (parser.isSet("keep-awake") || parser.isSet("no-keep-awake")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::KeepAwake);
    }

    // Resolve --performance-overlay and --no-performance-overlay options
    preferences->showPerformanceOverlay = parser.getToggleOptionValue("performance-overlay", preferences->showPerformanceOverlay);
    if (parser.isSet("performance-overlay") || parser.isSet("no-performance-overlay")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::PerformanceOverlay);
    }

    // Resolve --hdr and --no-hdr options
    preferences->enableHdr = parser.getToggleOptionValue("hdr", preferences->enableHdr);
    if (parser.isSet("hdr") || parser.isSet("no-hdr")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Hdr);
    }

    // Resolve --yuv444 and --no-yuv444 options
    preferences->enableYUV444 = parser.getToggleOptionValue("yuv444", preferences->enableYUV444);
    if (parser.isSet("yuv444") || parser.isSet("no-yuv444")) {
        request.cliOverrides.markExplicit(StreamLaunchOverrides::Yuv444);
    }
    
    // Resolve --capture-system-keys option
    if (parser.isSet("capture-system-keys")) {
        preferences->captureSysKeysMode = mapValue(m_CaptureSysKeysModeMap, parser.getChoiceOptionValue("capture-system-keys"));
        request.cliOverrides.markExplicit(StreamLaunchOverrides::CaptureSystemKeys);
    }

    // Resolve --video-codec option
    if (parser.isSet("video-codec")) {
        preferences->videoCodecConfig = mapValue(m_VideoCodecMap, parser.getChoiceOptionValue("video-codec"));
        request.cliOverrides.markExplicit(StreamLaunchOverrides::VideoCodec);
    }

    // Resolve --video-decoder option
    if (parser.isSet("video-decoder")) {
        preferences->videoDecoderSelection = mapValue(m_VideoDecoderMap, parser.getChoiceOptionValue("video-decoder"));
        request.cliOverrides.markExplicit(StreamLaunchOverrides::VideoDecoder);
    }

    // This method will not return and terminates the process if --version or
    // --help is specified
    parser.handleHelpAndVersionOptions();

    // Verify that both host and app has been provided
    auto posArgs = parser.positionalArguments();
    if (posArgs.length() < 2) {
        parser.showError("Host not provided");
    }
    request.host = parser.positionalArguments().at(1);

    if (posArgs.length() < 3) {
        parser.showError("App not provided");
    }
    request.appName = parser.positionalArguments().at(2);
    return request;
}

ListCommandLineParser::ListCommandLineParser()
{
}

ListCommandLineParser::~ListCommandLineParser()
{
}

void ListCommandLineParser::parse(const QStringList &args)
{
    CommandLineParser parser;
    parser.setupCommonOptions();
    parser.setApplicationDescription(
        "\n"
        "List the available apps on the given host."
    );
    parser.addPositionalArgument("list", "list available apps");
    parser.addPositionalArgument("host", "Host computer name, UUID, or IP address", "<host>");

    parser.addFlagOption("csv",     "Print as CSV with additional information");
    parser.addFlagOption("verbose", "Displays additional information");

    if (!parser.parse(args)) {
        parser.showError(parser.errorText());
    }

    parser.handleUnknownOptions();


    m_PrintCSV = parser.isSet("csv");
    m_Verbose = parser.isSet("verbose");

    // This method will not return and terminates the process if --version or
    // --help is specified
    parser.handleHelpAndVersionOptions();

    // Verify that host has been provided
    auto posArgs = parser.positionalArguments();
    if (posArgs.length() < 2) {
        parser.showError("Host not provided");
    }
    m_Host = parser.positionalArguments().at(1);
}

QString ListCommandLineParser::getHost() const
{
    return m_Host;
}

bool ListCommandLineParser::isPrintCSV() const
{
    return m_PrintCSV;
}

bool ListCommandLineParser::isVerbose() const
{
    return m_Verbose;
}
