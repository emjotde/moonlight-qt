#include "settings/appstreamingsettings.h"
#include "settings/streamingpreferences.h"
#include "cli/commandlineparser.h"
#include "backend/streamdisplays.h"
#include "streaming/input/keyboardrouting.h"

#include <QtTest>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>

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
    }

    void init()
    {
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
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(*global, profile));
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
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42)));
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
        QScopedPointer<StreamingPreferences> custom(AppStreamingSettings::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42)));
        QCOMPARE(custom->bitrateKbps, 65000);
        profile.bitrateKbps = 0;
        QCOMPARE(AppStreamingSettings::save(settings, "host-a", 42, profile), QString());
        StreamingPreferences::get()->bitrateKbps = 22000;
        QScopedPointer<StreamingPreferences> inherited(AppStreamingSettings::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(settings, "host-a", 42)));
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
        StreamingPreferences cli(*global);
        StreamCommandLineParser parser;
        parser.parse(QStringList({"moonlight", "stream", "host-a", "Desktop", "--video-codec", "HEVC", "--no-vsync"}) + options, &cli);
        auto profile = portraitProfile();
        profile.bitrateKbps = 65000;
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(*global, profile, &cli, parser.getExplicitOptions()));
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
        StreamingPreferences cli(*global);
        StreamCommandLineParser parser;
        parser.parse({"moonlight", "stream", "host-a", "Desktop", "--resolution", "2160x3840", "--fps", "120"}, &cli);
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(*global, AppStreamingOverride(), &cli, parser.getExplicitOptions()));
        QCOMPARE(effective->bitrateKbps, StreamingPreferences::getDefaultBitrate(2160, 3840, 120, false));
        QCOMPARE(effective->width, 2160);
        QCOMPARE(effective->height, 3840);

        QScopedPointer<StreamingPreferences> withProfile(AppStreamingSettings::resolve(*global, portraitProfile(), &cli, parser.getExplicitOptions()));
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
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(
            *StreamingPreferences::get(), AppStreamingSettings::load(reopened, "host-a", 42)));
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
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(*global, saved));
        QCOMPARE(effective->windowMode, StreamingPreferences::WM_FULLSCREEN_DESKTOP);
        QCOMPARE(effective->captureSysKeysMode, StreamingPreferences::CSK_FULLSCREEN);
        QCOMPARE(effective->preferredDisplay, QString("portrait-monitor"));

        StreamingPreferences cli(*global);
        StreamCommandLineParser parser;
        parser.parse({"moonlight", "stream", "host-a", "Desktop", "--display-mode", "windowed",
                      "--capture-system-keys", "never"}, &cli);
        QScopedPointer<StreamingPreferences> explicitValues(
            AppStreamingSettings::resolve(*global, saved, &cli, parser.getExplicitOptions()));
        QCOMPARE(explicitValues->windowMode, StreamingPreferences::WM_WINDOWED);
        QCOMPARE(explicitValues->captureSysKeysMode, StreamingPreferences::CSK_OFF);
        QCOMPARE(explicitValues->preferredDisplay, QString("portrait-monitor"));
        QCOMPARE(global->windowMode, oldWindowMode);
        QCOMPARE(global->captureSysKeysMode, oldCaptureMode);
        QVERIFY(global->preferredDisplay.isEmpty());
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
        QScopedPointer<StreamingPreferences> effective(AppStreamingSettings::resolve(*global, saved));
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
