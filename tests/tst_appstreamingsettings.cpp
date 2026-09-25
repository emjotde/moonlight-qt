#include "settings/appstreamingsettings.h"
#include "settings/streamingpreferences.h"
#include "settings/externallaunchtrust.h"
#include "cli/commandlineparser.h"
#include "backend/streamdisplays.h"
#include "backend/nvapp.h"
#include "streaming/input/keyboardrouting.h"
#include "streaming/urilaunchrequest.h"

#include <QtTest>
#include <QGuiApplication>
#include <QFile>
#include <QMetaProperty>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QSignalSpy>

static AppStreamingOverride portraitProfile()
{
    AppStreamingOverride profile;
    profile.enabled = true;
    profile.width = 2160;
    profile.height = 3840;
    profile.fps = 60;
    return profile;
}

static QVariantMap settingsSnapshot(QSettings& settings)
{
    QVariantMap values;
    for (const QString& key : settings.allKeys()) {
        values.insert(key, settings.value(key));
    }
    return values;
}

class DialogModel : public QObject
{
    Q_OBJECT
public:
    bool failWrites = false;
    int lastAppId = 0;

    Q_INVOKABLE QVariantMap getAppStreamingSettings(int appId)
    {
        QSettings settings;
        const auto profile = AppStreamingSettings::load(settings, "host-a", appId);
        return {
            {"enabled", profile.enabled},
            {"width", profile.enabled ? profile.width : 1920},
            {"height", profile.enabled ? profile.height : 1080},
            {"fps", profile.enabled ? profile.fps : 60},
            {"bitrateKbps", profile.bitrateKbps},
            {"globalBitrateKbps", 17000},
            {"windowMode", profile.windowMode},
            {"captureSysKeysMode", profile.captureSysKeysMode},
            {"preferredDisplay", profile.preferredDisplay},
            {"error", QString()},
        };
    }

    Q_INVOKABLE QVariantList getStreamDisplays()
    {
        return {QVariantMap{{"id", "landscape-monitor"}, {"text", "Landscape monitor"}},
                QVariantMap{{"id", "portrait-monitor"}, {"text", "Portrait monitor"}}};
    }

    Q_INVOKABLE QString saveAppStreamingSettings(int appId, int width, int height, int fps, int bitrate,
                                                int windowMode, int keyboardMode, QString preferredDisplay)
    {
        lastAppId = appId;
        if (failWrites) {
            return "Configuration is read-only";
        }
        auto profile = portraitProfile();
        profile.width = width;
        profile.height = height;
        profile.fps = fps;
        profile.bitrateKbps = bitrate;
        profile.windowMode = windowMode;
        profile.captureSysKeysMode = keyboardMode;
        profile.preferredDisplay = preferredDisplay;
        QSettings settings;
        return AppStreamingSettings::save(settings, "host-a", appId, profile);
    }

    Q_INVOKABLE QString removeAppStreamingSettings(int appId)
    {
        lastAppId = appId;
        if (failWrites) {
            return "Configuration is read-only";
        }
        QSettings settings;
        return AppStreamingSettings::remove(settings, "host-a", appId);
    }
};

class AppStreamingSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_SettingsDir.isValid());
        QCoreApplication::setOrganizationName("MoonlightProfileTests");
        QCoreApplication::setApplicationName("IsolatedSettings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_SettingsDir.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_SettingsDir.path());
        QSettings settings;
        m_SettingsFile = settings.fileName();
    }

    void init()
    {
        QVERIFY(QFile::remove(m_SettingsFile) || !QFile::exists(m_SettingsFile));
        QVERIFY(QFile::remove(m_SettingsFile + ".lock") || !QFile::exists(m_SettingsFile + ".lock"));
        QSettings settings;
        settings.clear();
        settings.setValue("width", 1920);
        settings.setValue("height", 1080);
        settings.setValue("fps", 60);
        settings.setValue("bitrate", 17000);
        settings.setValue("packetsize", 1232);
        settings.setValue("defaultver", 2);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        StreamingPreferences::get()->reload();
    }

    void noProfilePreservesEveryPreference()
    {
        auto global = StreamingPreferences::get();
        global->enableHdr = true;
        global->enableYUV444 = true;
        global->videoCodecConfig = StreamingPreferences::VCC_FORCE_AV1;
        global->absoluteMouseMode = true;
        global->captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        QSettings settings;
        const auto before = settingsSnapshot(settings);
        const auto profile = AppStreamingSettings::load(settings, "host-a", 42);
        QVERIFY(!profile.enabled);
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, profile, StreamLaunchRequest()));
        for (int i = global->metaObject()->propertyOffset(); i < global->metaObject()->propertyCount(); ++i) {
            const QMetaProperty property = global->metaObject()->property(i);
            QCOMPARE(property.read(effective.data()), property.read(global));
        }
        QCOMPARE(effective->packetSize, 1232);
        effective->width = 256;
        QCOMPARE(global->width, 1920);
        QCOMPARE(settingsSnapshot(settings), before);
    }

    void resolutions_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("landscape") << 3840 << 2160;
        QTest::newRow("portrait") << 2160 << 3840;
        QTest::newRow("custom") << 1728 << 3072;
        QTest::newRow("minimum") << 256 << 256;
        QTest::newRow("maximum") << 8192 << 8192;
    }

    void resolutions()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        auto profile = portraitProfile();
        profile.width = width;
        profile.height = height;
        QSettings settings;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        QScopedPointer<StreamingPreferences> effective(StreamLaunchPreferences::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42),
            StreamLaunchRequest()));
        QCOMPARE(effective->width, width);
        QCOMPARE(effective->height, height);
        QCOMPARE(effective->fps, 60);
        QCOMPARE(effective->bitrateKbps, 17000);
        QCOMPARE(StreamingPreferences::get()->width, 1920);
    }

    void optionalBitrate()
    {
        QSettings settings;
        auto profile = portraitProfile();
        profile.bitrateKbps = 65000;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        QScopedPointer<StreamingPreferences> custom(StreamLaunchPreferences::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42),
            StreamLaunchRequest()));
        QCOMPARE(custom->bitrateKbps, 65000);
        profile.bitrateKbps = 0;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        StreamingPreferences::get()->bitrateKbps = 22000;
        QScopedPointer<StreamingPreferences> inherited(StreamLaunchPreferences::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42),
            StreamLaunchRequest()));
        QCOMPARE(inherited->bitrateKbps, 22000);
        QCOMPARE(AppStreamingSettings::load(settings, "host-a", 42).bitrateKbps, 0);
    }

    void cliPrecedence_data()
    {
        QTest::addColumn<QStringList>("options");
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("fps");
        QTest::addColumn<int>("bitrate");
        QTest::newRow("none") << QStringList() << 2160 << 3840 << 60 << 65000;
        QTest::newRow("resolution") << QStringList({"--resolution", "1920x1080"}) << 1920 << 1080 << 60 << 65000;
        QTest::newRow("fps") << QStringList({"--fps", "120"}) << 2160 << 3840 << 120 << 65000;
        QTest::newRow("bitrate") << QStringList({"--bitrate", "17000"}) << 2160 << 3840 << 60 << 17000;
        QTest::newRow("all") << QStringList({"--resolution", "1200x1920", "--fps", "90", "--bitrate", "80000"})
                             << 1200 << 1920 << 90 << 80000;
        QTest::newRow("720") << QStringList({"--720"}) << 1280 << 720 << 60 << 65000;
        QTest::newRow("1080") << QStringList({"--1080"}) << 1920 << 1080 << 60 << 65000;
        QTest::newRow("1440") << QStringList({"--1440"}) << 2560 << 1440 << 60 << 65000;
        QTest::newRow("4K") << QStringList({"--4K"}) << 3840 << 2160 << 60 << 65000;
        QTest::newRow("last-resolution") << QStringList({"--4K", "--resolution", "1200x1920"})
                                         << 1200 << 1920 << 60 << 65000;
    }

    void cliPrecedence()
    {
        QFETCH(QStringList, options);
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, fps);
        QFETCH(int, bitrate);
        auto global = StreamingPreferences::get();
        StreamCommandLineParser parser;
        const auto request = parser.parse(
            QStringList({"moonlight", "stream", "host-a", "Desktop",
                         "--video-codec", "HEVC", "--no-vsync"}) + options,
            *global);
        auto profile = portraitProfile();
        profile.bitrateKbps = 65000;
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, profile, request));
        QCOMPARE(effective->width, width);
        QCOMPARE(effective->height, height);
        QCOMPARE(effective->fps, fps);
        QCOMPARE(effective->bitrateKbps, bitrate);
        QCOMPARE(effective->videoCodecConfig, StreamingPreferences::VCC_FORCE_HEVC);
        QVERIFY(!effective->enableVsync);
        QCOMPARE(global->width, 1920);
        QCOMPARE(global->height, 1080);
        QCOMPARE(global->bitrateKbps, 17000);
        QVERIFY(global->enableVsync);
    }

    void cliWithoutProfileKeepsLegacyBitrate()
    {
        auto global = StreamingPreferences::get();
        StreamCommandLineParser parser;
        const auto request = parser.parse(
            {"moonlight", "stream", "host-a", "Desktop",
             "--resolution", "2160x3840", "--fps", "120"},
            *global);
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, AppStreamingOverride(), request));
        QCOMPARE(effective->bitrateKbps, StreamingPreferences::getDefaultBitrate(2160, 3840, 120, false));
        QCOMPARE(effective->width, 2160);
        QCOMPARE(effective->height, 3840);

        QScopedPointer<StreamingPreferences> withProfile(
            StreamLaunchPreferences::resolve(*global, portraitProfile(), request));
        QCOMPARE(withProfile->bitrateKbps, global->bitrateKbps);
        QCOMPARE(withProfile->fps, 120);
    }

    void resetRestoresGlobalSettings()
    {
        QSettings settings;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        QCOMPARE(AppStreamingSettings::remove(settings, "host-a", 42), QString());
        StreamingPreferences::get()->width = 2560;
        QSettings reopened;
        QScopedPointer<StreamingPreferences> effective(StreamLaunchPreferences::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(reopened, "host-a", 42),
            StreamLaunchRequest()));
        QCOMPARE(effective->width, 2560);
        QCOMPARE(effective->height, 1080);
        QCOMPARE(effective->bitrateKbps, 17000);
        QVERIFY(!reopened.childGroups().contains("appstreamingprofiles"));
    }

    void identifiersRemainIndependent()
    {
        QSettings settings;
        auto landscape = portraitProfile();
        landscape.width = 3840;
        landscape.height = 2160;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        QCOMPARE(AppStreamingSettings::save(settings, "host-b", 42, landscape), QString());
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 43, landscape), QString());
        QCOMPARE(AppStreamingSettings::load(settings, "host-a", 42).height, 3840);
        QCOMPARE(AppStreamingSettings::load(settings, "host-b", 42).height, 2160);
        QCOMPARE(AppStreamingSettings::load(settings, "host-a", 43).height, 2160);
        QCOMPARE(AppStreamingSettings::remove(settings, "host-a", 42), QString());
        QVERIFY(AppStreamingSettings::load(settings, "host-b", 42).enabled);
        QVERIFY(AppStreamingSettings::load(settings, "host-a", 43).enabled);
        QVERIFY(!AppStreamingSettings::load(settings, "host-c", 42).enabled);
    }

    void survivesProcessRestart()
    {
        QSettings settings;
        auto profile = portraitProfile();
        profile.windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
        profile.captureSysKeysMode = StreamingPreferences::CSK_FULLSCREEN;
        profile.preferredDisplay = "portrait-monitor";
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {"--read-profile", settings.fileName()});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
    }

    void existingSettingsStayUntouched()
    {
        QSettings settings;
        settings.setValue("hosts/1/uuid", "existing-host");
        settings.setValue("hosts/1/apps/1/name", "Renamed Desktop");
        settings.setValue("custom-settings/unrelated", "keep");
        const auto before = settingsSnapshot(settings);
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        StreamingPreferences::get()->reload();
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            QCOMPARE(settings.value(it.key()), it.value());
        }
        QCOMPARE(AppStreamingSettings::remove(settings, "host-a", 42), QString());
        QCOMPARE(settingsSnapshot(settings), before);
    }

    void invalidValues_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("fps");
        QTest::addColumn<int>("bitrate");
        QTest::newRow("missing-width") << 0 << 3840 << 60 << 0;
        QTest::newRow("missing-height") << 2160 << 0 << 60 << 0;
        QTest::newRow("width-small") << 255 << 3840 << 60 << 0;
        QTest::newRow("height-small") << 2160 << 255 << 60 << 0;
        QTest::newRow("width-large") << 8193 << 3840 << 60 << 0;
        QTest::newRow("height-large") << 2160 << 8193 << 60 << 0;
        QTest::newRow("negative") << -2160 << 3840 << 60 << 0;
        QTest::newRow("fps-small") << 2160 << 3840 << 9 << 0;
        QTest::newRow("fps-large") << 2160 << 3840 << 10000 << 0;
        QTest::newRow("bitrate-small") << 2160 << 3840 << 60 << 499;
        QTest::newRow("bitrate-large") << 2160 << 3840 << 60 << 500001;
        QTest::newRow("bitrate-negative") << 2160 << 3840 << 60 << -1;
    }

    void invalidValues()
    {
        QFETCH(int, width);
        QFETCH(int, height);
        QFETCH(int, fps);
        QFETCH(int, bitrate);
        QSettings settings;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        const auto before = settingsSnapshot(settings);
        auto profile = portraitProfile();
        profile.width = width;
        profile.height = height;
        profile.fps = fps;
        profile.bitrateKbps = bitrate;
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 42, profile).isEmpty());
        QCOMPARE(settingsSnapshot(settings), before);
    }

    void corruptStoredValuesAreRejected()
    {
        QSettings settings;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        const QString prefix = "appstreamingprofiles/" + QString::fromLatin1(QByteArray("host-a").toHex()) + "/42/";
        settings.remove(prefix + "height");
        QString error;
        QVERIFY(!AppStreamingSettings::load(settings, "host-a", 42, &error).enabled);
        QVERIFY(!error.isEmpty());
        settings.setValue(prefix + "height", "not-a-number");
        QVERIFY(!AppStreamingSettings::load(settings, "host-a", 42, &error).enabled);
        QVERIFY(!error.isEmpty());
    }

    void invalidIdentifiersAndWriteFailures()
    {
        QSettings settings;
        const auto before = settingsSnapshot(settings);
        QVERIFY(!AppStreamingSettings::save(settings, "", 42, portraitProfile()).isEmpty());
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 0, portraitProfile()).isEmpty());
        QVERIFY(!AppStreamingSettings::remove(settings, "", 42).isEmpty());
        QCOMPARE(settingsSnapshot(settings), before);
        QSettings unwritable(m_SettingsDir.path(), QSettings::IniFormat);
        QVERIFY(!AppStreamingSettings::save(unwritable, "host-a", 42, portraitProfile()).isEmpty());
    }

    void desktopPreferencesAndCliPrecedence()
    {
        QSettings settings;
        auto profile = portraitProfile();
        profile.windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
        profile.captureSysKeysMode = StreamingPreferences::CSK_FULLSCREEN;
        profile.preferredDisplay = "portrait-monitor";
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        const auto saved = AppStreamingSettings::load(settings, "host-a", 42);
        auto global = StreamingPreferences::get();
        const auto oldWindowMode = global->windowMode;
        const auto oldCaptureMode = global->captureSysKeysMode;
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, saved, StreamLaunchRequest()));
        QCOMPARE(effective->windowMode, StreamingPreferences::WM_FULLSCREEN_DESKTOP);
        QCOMPARE(effective->captureSysKeysMode, StreamingPreferences::CSK_FULLSCREEN);
        QCOMPARE(effective->preferredDisplay, QString("portrait-monitor"));

        StreamCommandLineParser parser;
        const auto request = parser.parse(
            {"moonlight", "stream", "host-a", "Desktop", "--display-mode", "windowed",
             "--capture-system-keys", "never"},
            *global);
        QScopedPointer<StreamingPreferences> explicitValues(
            StreamLaunchPreferences::resolve(*global, saved, request));
        QCOMPARE(explicitValues->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(explicitValues->captureSysKeysMode, StreamingPreferences::CSK_OFF);
        QCOMPARE(explicitValues->preferredDisplay, QString("portrait-monitor"));
        QCOMPARE(global->windowMode, oldWindowMode);
        QCOMPARE(global->captureSysKeysMode, oldCaptureMode);
        QVERIFY(global->preferredDisplay.isEmpty());
    }

    void typedLaunchRequestLayering()
    {
        auto global = StreamingPreferences::get();
        global->width = 1280;
        global->height = 720;
        global->fps = 30;
        global->bitrateKbps = 10000;
        global->windowMode = StreamingPreferences::WM_WINDOWED;
        global->videoCodecConfig = StreamingPreferences::VCC_AUTO;

        auto profile = portraitProfile();
        profile.bitrateKbps = 65000;
        profile.windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;

        StreamCommandLineParser parser;
        auto request = parser.parse(
            {"moonlight", "stream", "host-a", "Desktop",
             "--resolution", "1920x1080", "--fps", "90", "--video-codec", "HEVC"},
            *global);
        QCOMPARE(request.host, QString("host-a"));
        QCOMPARE(request.appName, QString("Desktop"));
        QVERIFY(!request.hasAppId());

        request.uriOverrides = StreamLaunchOverrides(*global);
        request.uriOverrides.values()->width = 2160;
        request.uriOverrides.values()->height = 3840;
        request.uriOverrides.markExplicit(StreamLaunchOverrides::Resolution);
        request.uriOverrides.values()->bitrateKbps = 80000;
        request.uriOverrides.markExplicit(StreamLaunchOverrides::Bitrate);
        request.uriOverrides.values()->windowMode = StreamingPreferences::WM_FULLSCREEN;
        request.uriOverrides.markExplicit(StreamLaunchOverrides::WindowMode);

        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, profile, request));
        QCOMPARE(effective->width, 2160);
        QCOMPARE(effective->height, 3840);
        QCOMPARE(effective->fps, 90);
        QCOMPARE(effective->bitrateKbps, 80000);
        QCOMPARE(effective->windowMode, StreamingPreferences::WM_FULLSCREEN);
        QCOMPARE(effective->videoCodecConfig, StreamingPreferences::VCC_FORCE_HEVC);

        QCOMPARE(global->width, 1280);
        QCOMPARE(global->height, 720);
        QCOMPARE(global->fps, 30);
        QCOMPARE(global->bitrateKbps, 10000);
        QCOMPARE(global->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(global->videoCodecConfig, StreamingPreferences::VCC_AUTO);
    }

    void validLandscapeUri()
    {
        const auto result = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Landscape%20Desktop&resolution=3840x2160"
            "&fps=60&displayMode=fullscreen",
            *StreamingPreferences::get());
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QCOMPARE(result.request.host, QString("host-a"));
        QCOMPARE(result.request.appName, QString("Landscape Desktop"));
        const auto values = result.request.uriOverrides.values();
        QCOMPARE(values->width, 3840);
        QCOMPARE(values->height, 2160);
        QCOMPARE(values->fps, 60);
        QCOMPARE(values->windowMode, StreamingPreferences::WM_FULLSCREEN);
    }

    void validPortraitUri()
    {
        const auto result = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Portrait%20Desktop&resolution=2160x3840"
            "&fps=60&bitrate=80000&displayMode=borderless&codec=HEVC"
            "&audioConfig=7.1-surround&quitAfter=false",
            *StreamingPreferences::get());
        QVERIFY2(result.isValid(), qPrintable(result.error));
        const auto values = result.request.uriOverrides.values();
        QCOMPARE(values->width, 2160);
        QCOMPARE(values->height, 3840);
        QCOMPARE(values->fps, 60);
        QCOMPARE(values->bitrateKbps, 80000);
        QCOMPARE(values->windowMode, StreamingPreferences::WM_FULLSCREEN_DESKTOP);
        QCOMPARE(values->videoCodecConfig, StreamingPreferences::VCC_FORCE_HEVC);
        QCOMPARE(values->audioConfig, StreamingPreferences::AC_71_SURROUND);
        QVERIFY(!values->quitAppAfter);
    }

    void percentDecodingHappensOnce()
    {
        const auto decoded = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=My%20Desktop%20%E2%98%83",
            *StreamingPreferences::get());
        QVERIFY2(decoded.isValid(), qPrintable(decoded.error));
        QCOMPARE(decoded.request.appName, QString::fromUtf8("My Desktop \xE2\x98\x83"));

        const auto once = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=My%2520Desktop",
            *StreamingPreferences::get());
        QVERIFY2(once.isValid(), qPrintable(once.error));
        QCOMPARE(once.request.appName, QString("My%20Desktop"));
    }

    void appIdPrecedesNameThenFallsBack()
    {
        const auto result = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Wrong%20Name&appId=42",
            *StreamingPreferences::get());
        QVERIFY2(result.isValid(), qPrintable(result.error));
        QVector<NvApp> apps;
        NvApp nameMatch;
        nameMatch.id = 41;
        nameMatch.name = "Wrong Name";
        apps.append(nameMatch);
        NvApp idMatch;
        idMatch.id = 42;
        idMatch.name = "Correct by ID";
        apps.append(idMatch);
        QCOMPARE(result.request.findAppIndex(apps), 1);

        apps.removeLast();
        QCOMPARE(result.request.findAppIndex(apps), 0);
    }

    void uriOverridesCliAndProfile()
    {
        auto global = StreamingPreferences::get();
        global->width = 1280;
        global->height = 720;
        global->fps = 30;
        global->bitrateKbps = 10000;

        StreamCommandLineParser cliParser;
        auto request = cliParser.parse(
            {"moonlight", "stream", "host-a", "Desktop",
             "--resolution", "1920x1080", "--fps", "90", "--bitrate", "30000"},
            *global);
        const auto uri = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Desktop&resolution=2160x3840"
            "&fps=60&bitrate=80000",
            *global);
        QVERIFY2(uri.isValid(), qPrintable(uri.error));
        request.uriOverrides = uri.request.uriOverrides;
        auto profile = portraitProfile();
        profile.width = 3840;
        profile.height = 2160;
        profile.fps = 120;
        profile.bitrateKbps = 65000;

        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, profile, request));
        QCOMPARE(effective->width, 2160);
        QCOMPARE(effective->height, 3840);
        QCOMPARE(effective->fps, 60);
        QCOMPARE(effective->bitrateKbps, 80000);
        QCOMPARE(global->width, 1280);
        QCOMPARE(global->height, 720);
    }

    void uriMissingOptionalValuesUsesProfileAndGlobal()
    {
        auto global = StreamingPreferences::get();
        global->bitrateKbps = 17000;
        global->videoCodecConfig = StreamingPreferences::VCC_FORCE_AV1;
        const auto uri = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Desktop&fps=90",
            *global);
        QVERIFY2(uri.isValid(), qPrintable(uri.error));
        auto profile = portraitProfile();
        profile.bitrateKbps = 0;
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, profile, uri.request));
        QCOMPARE(effective->width, 2160);
        QCOMPARE(effective->height, 3840);
        QCOMPARE(effective->fps, 90);
        QCOMPARE(effective->bitrateKbps, 17000);
        QCOMPARE(effective->videoCodecConfig, StreamingPreferences::VCC_FORCE_AV1);
    }

    void invalidUris_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("errorFragment");
        QTest::newRow("invalid-resolution-format")
            << "moonlight://stream?host=h&app=a&resolution=3840-2160" << "WIDTHxHEIGHT";
        QTest::newRow("invalid-resolution-range")
            << "moonlight://stream?host=h&app=a&resolution=200x2160" << "between 256 and 8192";
        QTest::newRow("fps-low")
            << "moonlight://stream?host=h&app=a&fps=9" << "between 10 and 480";
        QTest::newRow("fps-high")
            << "moonlight://stream?host=h&app=a&fps=481" << "between 10 and 480";
        QTest::newRow("bitrate-low")
            << "moonlight://stream?host=h&app=a&bitrate=499" << "between 500 and 500000";
        QTest::newRow("bitrate-high")
            << "moonlight://stream?host=h&app=a&bitrate=500001" << "between 500 and 500000";
        QTest::newRow("display-mode")
            << "moonlight://stream?host=h&app=a&displayMode=maximized" << "displayMode";
        QTest::newRow("codec")
            << "moonlight://stream?host=h&app=a&codec=VP9" << "codec";
        QTest::newRow("audio")
            << "moonlight://stream?host=h&app=a&audioConfig=quad" << "audioConfig";
        QTest::newRow("boolean")
            << "moonlight://stream?host=h&app=a&quitAfter=1" << "true or false";
        QTest::newRow("duplicate")
            << "moonlight://stream?host=h&host=h2&app=a" << "Duplicate";
        QTest::newRow("malformed-percent")
            << "moonlight://stream?host=h&app=Bad%2Name" << "percent";
        QTest::newRow("unknown-action")
            << "moonlight://quit?host=h&app=a" << "Unknown";
        QTest::newRow("unknown-parameter")
            << "moonlight://stream?host=h&app=a&shell=cmd" << "Unknown URI parameter";
        QTest::newRow("missing-host")
            << "moonlight://stream?app=a" << "host";
        QTest::newRow("missing-app")
            << "moonlight://stream?host=h" << "app or appId";
        QTest::newRow("invalid-app-id")
            << "moonlight://stream?host=h&appId=-1" << "application ID";
        QTest::newRow("fragment")
            << "moonlight://stream?host=h&app=a#fragment" << "fragments";
        QTest::newRow("empty-pair")
            << "moonlight://stream?host=h&app=a&" << "empty query";
    }

    void invalidUris()
    {
        QFETCH(QString, uri);
        QFETCH(QString, errorFragment);
        const auto result = UriLaunchRequestParser::parse(uri, *StreamingPreferences::get());
        QVERIFY(!result.isValid());
        QVERIFY2(result.error.contains(errorFragment, Qt::CaseInsensitive),
                 qPrintable(result.error));
    }

    void embeddedNulIsRejected()
    {
        QString uri = "moonlight://stream?host=h&app=Desktop";
        uri.append(QChar('\0'));
        const auto result = UriLaunchRequestParser::parse(uri, *StreamingPreferences::get());
        QVERIFY(!result.isValid());
        QVERIFY(result.error.contains("control", Qt::CaseInsensitive));

        const auto encoded = UriLaunchRequestParser::parse(
            "moonlight://stream?host=h&app=Desktop%00Hidden",
            *StreamingPreferences::get());
        QVERIFY(!encoded.isValid());
        QVERIFY(encoded.error.contains("control", Qt::CaseInsensitive));
    }

    void globalParserAcceptsUriOption()
    {
        GlobalCommandLineParser parser;
        QCOMPARE(parser.parse({"moonlight", "--uri",
                               "moonlight://stream?host=h&app=Desktop"}),
                 GlobalCommandLineParser::UriRequested);
        QCOMPARE(parser.getUri(), QString("moonlight://stream?host=h&app=Desktop"));
    }

    void launchRequestSourcesAreTyped()
    {
        StreamCommandLineParser cliParser;
        const auto cli = cliParser.parse(
            {"moonlight", "stream", "host-a", "Desktop"},
            *StreamingPreferences::get());
        QCOMPARE(cli.source, StreamLaunchRequest::CliSource);

        const auto uri = UriLaunchRequestParser::parse(
            "moonlight://stream?host=host-a&app=Desktop",
            *StreamingPreferences::get());
        QVERIFY2(uri.isValid(), qPrintable(uri.error));
        QCOMPARE(uri.request.source, StreamLaunchRequest::UriSource);
        QCOMPARE(StreamLaunchRequest().source, StreamLaunchRequest::GuiSource);
    }

    void externalLaunchConfirmationSettingPersists()
    {
        auto preferences = StreamingPreferences::get();
        QVERIFY(preferences->confirmExternalLaunchRequests);
        preferences->confirmExternalLaunchRequests = false;
        preferences->save();
        preferences->confirmExternalLaunchRequests = true;
        preferences->reload();
        QVERIFY(!preferences->confirmExternalLaunchRequests);
    }

    void externalLaunchTrustUsesStableHostUuid()
    {
        QSettings settings;
        QVERIFY(!ExternalLaunchTrust::isTrusted(settings, "stable-host-uuid"));
        QCOMPARE(ExternalLaunchTrust::trustHost(settings, "stable-host-uuid"), QString());
        QVERIFY(ExternalLaunchTrust::isTrusted(settings, "stable-host-uuid"));
        QVERIFY(!ExternalLaunchTrust::isTrusted(settings, "192.0.2.10"));
        QVERIFY(!ExternalLaunchTrust::isTrusted(settings, "other-host-uuid"));

        QSettings reopened;
        QVERIFY(ExternalLaunchTrust::isTrusted(reopened, "stable-host-uuid"));
        QCOMPARE(ExternalLaunchTrust::removeHostTrust(reopened, "stable-host-uuid"), QString());
        QVERIFY(!ExternalLaunchTrust::isTrusted(reopened, "stable-host-uuid"));
    }

    void externalLaunchConfirmationPolicy_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::addColumn<bool>("paired");
        QTest::addColumn<bool>("trusted");
        QTest::addColumn<bool>("expected");
        QTest::newRow("default") << true << true << false << true;
        QTest::newRow("trusted") << true << true << true << false;
        QTest::newRow("disabled") << false << true << false << false;
        QTest::newRow("unpaired") << true << false << false << false;
    }

    void externalLaunchConfirmationPolicy()
    {
        QFETCH(bool, enabled);
        QFETCH(bool, paired);
        QFETCH(bool, trusted);
        QFETCH(bool, expected);
        QCOMPARE(ExternalLaunchTrust::shouldConfirm(enabled, paired, trusted), expected);
    }

    void externalLaunchDialogShowsDetailsAndCancels()
    {
        QQmlEngine engine;
        QQuickItem focusTarget;
        engine.rootContext()->setContextProperty("stackView", &focusTarget);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick 2.9
            import QtQuick.Controls 2.2
            ApplicationWindow {
                width: 900; height: 600; visible: true
                ExternalLaunchDialog {
                    details: "Host: Test Host\nApplication: Portrait Desktop\nResolution: 2160 x 3840 at 60 FPS\nDisplay mode: Fullscreen"
                }
            }
        )", QUrl::fromLocalFile(QStringLiteral(TEST_GUI_DIR "/ExternalLaunchTest.qml")));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto dialog = window->findChild<QObject*>("externalLaunchDialog");
        auto details = window->findChild<QObject*>("externalLaunchDetails");
        auto launchOnce = window->findChild<QObject*>("launchOnceButton");
        auto alwaysAllow = window->findChild<QObject*>("alwaysAllowButton");
        auto cancel = window->findChild<QObject*>("cancelExternalLaunchButton");
        QVERIFY(dialog && details && launchOnce && alwaysAllow && cancel);
        QVERIFY(details->property("text").toString().contains("Portrait Desktop"));
        QVERIFY(details->property("text").toString().contains("2160 x 3840 at 60 FPS"));
        QSignalSpy cancelled(dialog, SIGNAL(cancelled()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(cancelled.count(), 1);
    }

    void oldProfilesInheritDesktopPreferences()
    {
        QSettings settings;
        auto global = StreamingPreferences::get();
        global->windowMode = StreamingPreferences::WM_WINDOWED;
        global->captureSysKeysMode = StreamingPreferences::CSK_ALWAYS;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, portraitProfile()), QString());
        const auto saved = AppStreamingSettings::load(settings, "host-a", 42);
        QCOMPARE(saved.windowMode, -1);
        QCOMPARE(saved.captureSysKeysMode, -1);
        QVERIFY(saved.preferredDisplay.isEmpty());
        QScopedPointer<StreamingPreferences> effective(
            StreamLaunchPreferences::resolve(*global, saved, StreamLaunchRequest()));
        QCOMPARE(effective->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(effective->captureSysKeysMode, StreamingPreferences::CSK_ALWAYS);
    }

    void rejectInvalidDesktopPreferences()
    {
        QSettings settings;
        const auto before = settingsSnapshot(settings);
        auto profile = portraitProfile();
        profile.windowMode = 10;
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 42, profile).isEmpty());
        profile.windowMode = -2;
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 42, profile).isEmpty());
        profile.windowMode = -1;
        profile.captureSysKeysMode = 3;
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 42, profile).isEmpty());
        profile.captureSysKeysMode = -1;
        profile.preferredDisplay = QString(1025, 'x');
        QVERIFY(!AppStreamingSettings::save(settings, "host-a", 42, profile).isEmpty());
        QCOMPARE(settingsSnapshot(settings), before);
    }

    void monitorIdentitySurvivesReordering()
    {
        StreamDisplay landscape, portrait;
        landscape.id = "landscape-monitor";
        portrait.id = "portrait-monitor";
        QCOMPARE(StreamDisplays::find({landscape, portrait}, "portrait-monitor"), 1);
        QCOMPARE(StreamDisplays::find({portrait, landscape}, "portrait-monitor"), 0);
        QCOMPARE(StreamDisplays::find({landscape}, "portrait-monitor"), -1);
        QCOMPARE(StreamDisplays::find({landscape, portrait}, ""), -1);
    }

    void keyboardCapturePolicy_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("fullscreen");
        QTest::addColumn<bool>("inputActive");
        QTest::addColumn<bool>("dockActive");
        for (int mode = 0; mode <= 2; ++mode) {
            for (int fullscreen = 0; fullscreen <= 1; ++fullscreen) {
                for (int active = 0; active <= 1; ++active) {
                    for (int dock = 0; dock <= 1; ++dock) {
                        const QByteArray name = QString("%1-fullscreen%2-active%3-dock%4")
                                                    .arg(mode).arg(fullscreen).arg(active).arg(dock).toLatin1();
                        QTest::newRow(name) << mode << bool(fullscreen) << bool(active) << bool(dock);
                    }
                }
            }
        }
    }

    void keyboardCapturePolicy()
    {
        QFETCH(int, mode);
        QFETCH(bool, fullscreen);
        QFETCH(bool, inputActive);
        QFETCH(bool, dockActive);
        const bool expected = inputActive && !dockActive &&
                              (mode == 2 || (mode == 1 && fullscreen));
        QCOMPARE(KeyboardRouting::shouldCapture(
                     static_cast<StreamingPreferences::CaptureSysKeysMode>(mode),
                     fullscreen, inputActive, dockActive), expected);
    }

    void localSnapConsumesWholeChord()
    {
        KeyboardRouting routing;
        SDL_KeyboardEvent event = {};
        event.type = SDL_KEYDOWN;
        event.state = SDL_PRESSED;
        event.keysym.scancode = SDL_SCANCODE_LGUI;
        QVERIFY(routing.consumeLocalShortcut(event, false));
        event.keysym.scancode = SDL_SCANCODE_RIGHT;
        event.keysym.mod = KMOD_LGUI | KMOD_LSHIFT;
        QVERIFY(routing.consumeLocalShortcut(event, false));
        event.type = SDL_KEYUP;
        event.state = SDL_RELEASED;
        event.keysym.scancode = SDL_SCANCODE_LGUI;
        event.keysym.mod = KMOD_NONE;
        QVERIFY(routing.consumeLocalShortcut(event, false));
        event.keysym.scancode = SDL_SCANCODE_RIGHT;
        QVERIFY(routing.consumeLocalShortcut(event, false));
        event.type = SDL_KEYDOWN;
        event.state = SDL_PRESSED;
        QVERIFY(!routing.consumeLocalShortcut(event, false));
    }

    void remoteShortcutsAreForwarded()
    {
        KeyboardRouting routing;
        SDL_KeyboardEvent event = {};
        event.type = SDL_KEYDOWN;
        event.keysym.scancode = SDL_SCANCODE_LGUI;
        QVERIFY(!routing.consumeLocalShortcut(event, true));
        event.keysym.scancode = SDL_SCANCODE_RIGHT;
        event.keysym.mod = KMOD_LGUI;
        QVERIFY(!routing.consumeLocalShortcut(event, true));
        event.type = SDL_KEYUP;
        QVERIFY(!routing.consumeLocalShortcut(event, true));
    }

    void dialogSavesValidatesAndResets()
    {
        QQmlEngine engine;
        DialogModel model;
        QQuickItem focusTarget;
        engine.rootContext()->setContextProperty("testAppModel", &model);
        engine.rootContext()->setContextProperty("stackView", &focusTarget);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick 2.9
            import QtQuick.Controls 2.2
            ApplicationWindow {
                width: 900; height: 600; visible: true
                AppStreamSettingsDialog {
                    objectName: "profileDialog"
                    appModel: testAppModel
                }
            }
        )", QUrl::fromLocalFile(QStringLiteral(TEST_GUI_DIR "/TestWindow.qml")));
        QScopedPointer<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto dialog = window->findChild<QObject*>("profileDialog");
        QVERIFY(dialog);
        auto useGlobal = dialog->findChild<QObject*>("useGlobal");
        auto width = dialog->findChild<QObject*>("widthField");
        auto height = dialog->findChild<QObject*>("heightField");
        auto fps = dialog->findChild<QObject*>("fpsField");
        auto bitrate = dialog->findChild<QObject*>("bitrateField");
        auto customBitrate = dialog->findChild<QObject*>("customBitrate");
        auto saveButton = dialog->findChild<QObject*>("saveButton");
        auto windowMode = dialog->findChild<QObject*>("windowMode");
        auto keyboardMode = dialog->findChild<QObject*>("keyboardMode");
        auto preferredDisplay = dialog->findChild<QObject*>("preferredDisplay");
        QVERIFY(useGlobal && width && height && fps && bitrate && customBitrate && saveButton &&
                windowMode && keyboardMode && preferredDisplay);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openForApp", Q_ARG(QVariant, 42), Q_ARG(QVariant, "Desktop")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("height").toInt() <= 560);
        QVERIFY(useGlobal->property("checked").toBool());
        useGlobal->setProperty("checked", false);
        width->setProperty("text", "");
        QVERIFY(!saveButton->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveProfile"));
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(model.lastAppId, 0);

        width->setProperty("text", "2160");
        height->setProperty("text", "3840");
        fps->setProperty("text", "60");
        customBitrate->setProperty("checked", true);
        bitrate->setProperty("text", "0");
        QVERIFY(!saveButton->property("enabled").toBool());
        bitrate->setProperty("text", "65000");
        windowMode->setProperty("currentIndex", 2);
        keyboardMode->setProperty("currentIndex", 1);
        preferredDisplay->setProperty("currentIndex", 2);
        QVERIFY(saveButton->property("enabled").toBool());
        model.failWrites = true;
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveProfile"));
        QVERIFY(dialog->property("visible").toBool());
        QCOMPARE(dialog->property("errorText").toString(), QString("Configuration is read-only"));
        model.failWrites = false;
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveProfile"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(model.lastAppId, 42);
        QSettings settings;
        const auto saved = AppStreamingSettings::load(settings, "host-a", 42);
        QCOMPARE(saved.width, 2160);
        QCOMPARE(saved.height, 3840);
        QCOMPARE(saved.bitrateKbps, 65000);
        QCOMPARE(saved.windowMode, int(StreamingPreferences::WM_FULLSCREEN_DESKTOP));
        QCOMPARE(saved.captureSysKeysMode, int(StreamingPreferences::CSK_FULLSCREEN));
        QCOMPARE(saved.preferredDisplay, QString("portrait-monitor"));

        QVERIFY(QMetaObject::invokeMethod(dialog, "openForApp", Q_ARG(QVariant, 42), Q_ARG(QVariant, "Renamed Desktop")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(!useGlobal->property("checked").toBool());
        QCOMPARE(height->property("text").toString(), QString("3840"));
        QCOMPARE(windowMode->property("currentIndex").toInt(), 2);
        QCOMPARE(keyboardMode->property("currentIndex").toInt(), 1);
        QCOMPARE(preferredDisplay->property("currentIndex").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        auto disconnected = saved;
        disconnected.preferredDisplay = "disconnected-monitor";
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, disconnected), QString());
        QVERIFY(QMetaObject::invokeMethod(dialog, "openForApp", Q_ARG(QVariant, 42), Q_ARG(QVariant, "Desktop")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QCOMPARE(preferredDisplay->property("currentIndex").toInt(), 3);
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveProfile"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(AppStreamingSettings::load(settings, "host-a", 42).preferredDisplay,
                 QString("disconnected-monitor"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "openForApp", Q_ARG(QVariant, 42), Q_ARG(QVariant, "Desktop")));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(dialog, "resetProfile"));
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(!AppStreamingSettings::load(settings, "host-a", 42).enabled);
    }

private:
    QTemporaryDir m_SettingsDir;
    QString m_SettingsFile;
};

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QGuiApplication application(argc, argv);
    if (application.arguments().value(1) == "--read-profile") {
        QSettings settings(application.arguments().value(2), QSettings::IniFormat);
        const auto profile = AppStreamingSettings::load(settings, "host-a", 42);
        return profile.enabled && profile.width == 2160 && profile.height == 3840 &&
                       profile.fps == 60 && profile.bitrateKbps == 0 &&
                       profile.windowMode == StreamingPreferences::WM_FULLSCREEN_DESKTOP &&
                       profile.captureSysKeysMode == StreamingPreferences::CSK_FULLSCREEN &&
                       profile.preferredDisplay == "portrait-monitor" ? 0 : 1;
    }
    AppStreamingSettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_appstreamingsettings.moc"
