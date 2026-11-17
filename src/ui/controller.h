// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hardwaretest/testanalysis.h"
#include "savegame/saveimport.h"
#include "settings/wheelsettings.h"
#include "statistics/jobtracker.h"
#include "statistics/sessionrecorder.h"
#include "statistics/sessionstore.h"
#include "telemetry/telemetryreceiver.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <memory>
#include <optional>
#include <utility>

class QJSEngine;
class QProcess;
class QQmlEngine;

namespace Wheelstar::Device
{
    class DeviceManager;
    class WheelDevice;
} // namespace Wheelstar::Device

namespace Wheelstar::HardwareTest
{
    class FfbTest;
} // namespace Wheelstar::HardwareTest

namespace Wheelstar::Ui
{
    /// Which LEDs of the five on the wheel rim light up for a peak force level (0x7fff = 100%): rising up to
    /// 100%, and the outer ones going dark again beyond it, to show clipping.
    constexpr int ffbMeterLeds(int level)
    {
        constexpr std::array<std::pair<int, int>, 9> Thresholds{{
            {2458, 0}, // < 7.5%
            {8192, 1}, // < 25%
            {16384, 3}, // < 50%
            {24576, 7}, // < 75%
            {29491, 15}, // < 90%
            {32769, 31}, // <= 100%
            {36045, 30}, // < 110%
            {40960, 28}, // < 125%
            {49152, 24}, // < 150%
        }};

        for (const auto& [limit, leds] : Thresholds)
        {
            if (level < limit)
            {
                return leds;
            }
        }

        return 16;
    }

    static_assert(ffbMeterLeds(0) == 0 && ffbMeterLeds(32768) == 31 && ffbMeterLeds(60000) == 16);

    /// What the command line asks of the GUI.
    struct StartupOptions
    {
        Settings::WheelSettings settings;
        QString profile;
        QString device;
        QStringList command;
        std::optional<bool> startManually;
    };

    /// Everything the QML windows show and do: the selected wheel, its settings and profiles, the live inputs,
    /// the overlay, the wheel buttons and the hardware tests. Upstream's gui.py, model.py and gtk_ui.py in one.
    class Controller : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(Wheelstar)
        QML_SINGLETON

        Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
        Q_PROPERTY(QString currentDeviceId READ currentDeviceId WRITE selectDevice NOTIFY deviceChanged)
        Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY deviceChanged)
        Q_PROPERTY(int maxRange READ maxRange NOTIFY deviceChanged)
        Q_PROPERTY(QVariantList modes READ modes NOTIFY deviceChanged)
        Q_PROPERTY(bool permissionsMissing READ permissionsMissing NOTIFY deviceChanged)

        Q_PROPERTY(QStringList profiles READ profiles NOTIFY profilesChanged)
        Q_PROPERTY(QString currentProfile READ currentProfile NOTIFY settingsChanged)
        Q_PROPERTY(bool profileDirty READ profileDirty NOTIFY settingsChanged)
        /// By profile key; a setting the wheel does not support is missing, so it reads as undefined in QML.
        Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
        /// What the user calls the buttons of this wheel, by button number as text ("0": "X"). Kept per wheel.
        Q_PROPERTY(QVariantMap buttonNames READ buttonNames NOTIFY buttonNamesChanged)

        Q_PROPERTY(double steering MEMBER m_steering NOTIFY inputChanged)
        Q_PROPERTY(double throttle MEMBER m_throttle NOTIFY inputChanged)
        Q_PROPERTY(double brake MEMBER m_brake NOTIFY inputChanged)
        Q_PROPERTY(double clutch MEMBER m_clutch NOTIFY inputChanged)
        Q_PROPERTY(int hatX MEMBER m_hatX NOTIFY inputChanged)
        Q_PROPERTY(int hatY MEMBER m_hatY NOTIFY inputChanged)
        Q_PROPERTY(quint32 buttons MEMBER m_buttons NOTIFY inputChanged)

        Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY overlayChanged)
        Q_PROPERTY(bool overlayShowsRange READ overlayShowsRange NOTIFY overlayChanged)
        Q_PROPERTY(bool overlayShowsFfbMeter READ overlayShowsFfbMeter NOTIFY overlayChanged)
        Q_PROPERTY(int ffbMeterLeds MEMBER m_ffbMeterLeds NOTIFY overlayChanged)

        Q_PROPERTY(QString buttonSetupText READ buttonSetupText NOTIFY buttonSetupChanged)
        Q_PROPERTY(bool checkPermissions READ checkPermissions WRITE setCheckPermissions NOTIFY preferencesChanged)
        Q_PROPERTY(bool showUnusedButtons READ showUnusedButtons WRITE setShowUnusedButtons NOTIFY preferencesChanged)
        Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY preferencesChanged)
        Q_PROPERTY(QVariantList languages READ languages CONSTANT)

        Q_PROPERTY(bool commandPending READ commandPending NOTIFY commandChanged)

        Q_PROPERTY(TestStage testStage READ testStage NOTIFY testChanged)
        Q_PROPERTY(bool hasTestReport READ hasTestReport NOTIFY testChanged)
        Q_PROPERTY(QVariantMap testResults READ testResults NOTIFY testChanged)

        /// Whether the Euro Truck Simulator 2 / American Truck Simulator plugin is installed: its package brings the
        /// istar-wheelstar-ets2-plugin command. Without it, the pages about the games have nothing to show.
        Q_PROPERTY(bool telemetryPluginInstalled MEMBER m_telemetryPluginInstalled CONSTANT)
        /// What the telemetry plugin in the game reports; only meaningful while telemetryActive.
        Q_PROPERTY(bool telemetryActive READ telemetryActive NOTIFY telemetryChanged)
        Q_PROPERTY(QString telemetryGame READ telemetryGame NOTIFY telemetryChanged)
        /// Kilometres per hour.
        Q_PROPERTY(double telemetrySpeed READ telemetrySpeed NOTIFY telemetryChanged)
        /// Positive forward, 0 neutral, negative reverse.
        Q_PROPERTY(int telemetryGear READ telemetryGear NOTIFY telemetryChanged)

        /// The recorded drives, newest first, each as a map; read in the background by refreshSessions().
        Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
        Q_PROPERTY(bool sessionsLoading READ sessionsLoading NOTIFY sessionsChanged)
        /// Why the database could not be read; empty when it could.
        Q_PROPERTY(QString sessionsError READ sessionsError NOTIFY sessionsChanged)
        /// The jobs, latest first by the game's clock, each as a map; read with the sessions.
        Q_PROPERTY(QVariantList jobs READ jobs NOTIFY sessionsChanged)

        /// The profiles of the games with a save, newest first: {name, game, savedAt}.
        Q_PROPERTY(QVariantList saveGameProfiles READ saveGameProfiles NOTIFY careerChanged)
        /// The career of the profile loadCareer() read; empty before.
        Q_PROPERTY(QVariantMap career READ career NOTIFY careerChanged)
        /// The company in the same save game: {garages, drivers, trucks, trailers, bank, repairs, …}; empty before.
        Q_PROPERTY(QVariantMap company READ company NOTIFY careerChanged)
        Q_PROPERTY(bool careerLoading READ careerLoading NOTIFY careerChanged)
        Q_PROPERTY(QString careerError READ careerError NOTIFY careerChanged)

    public:
        enum class TestStage
        {
            Idle,
            MinimumForcePrompt,
            MinimumForceReady,
            MinimumForceRunning,
            LinearityPrompt,
            StepPrompt,
            Running,
            Results,
        };
        Q_ENUM(TestStage)

        Controller(Device::DeviceManager* devices, StartupOptions options, QObject* parent = nullptr);
        ~Controller() override;

        Controller(const Controller&) = delete ("one instance, owned by main()");
        Controller& operator=(const Controller&) = delete ("one instance, owned by main()");
        Controller(Controller&&) = delete ("one instance, owned by main()");
        Controller& operator=(Controller&&) = delete ("one instance, owned by main()");

        static Controller* create(QQmlEngine* qmlEngine, QJSEngine* engine);

        /// The directory profiles are kept in.
        static QString profileDirectory();

        /// What a second launch asked for, handed over by KDBusService: a profile, settings, a companion command.
        void applyStartupOptions(const StartupOptions& options);

        [[nodiscard]] QVariantList devices() const;
        [[nodiscard]] QString currentDeviceId() const;
        void selectDevice(const QString& id);
        [[nodiscard]] bool hasDevice() const;
        [[nodiscard]] int maxRange() const;
        [[nodiscard]] QVariantList modes() const;
        [[nodiscard]] bool permissionsMissing() const;

        [[nodiscard]] QStringList profiles() const;
        [[nodiscard]] QString currentProfile() const;
        [[nodiscard]] bool profileDirty() const;
        [[nodiscard]] QVariantMap settings() const;
        [[nodiscard]] QVariantMap buttonNames() const;

        [[nodiscard]] bool overlayVisible() const;
        [[nodiscard]] bool overlayShowsRange() const;
        [[nodiscard]] bool overlayShowsFfbMeter() const;

        [[nodiscard]] QString buttonSetupText() const;
        [[nodiscard]] bool checkPermissions() const;
        void setCheckPermissions(bool check);
        [[nodiscard]] bool showUnusedButtons() const;
        void setShowUnusedButtons(bool show);
        [[nodiscard]] QString language() const;
        void setLanguage(const QString& language);
        [[nodiscard]] QVariantList languages() const;

        [[nodiscard]] bool commandPending() const;

        [[nodiscard]] TestStage testStage() const;
        [[nodiscard]] bool hasTestReport() const;
        [[nodiscard]] QVariantMap testResults() const;

        [[nodiscard]] bool telemetryActive() const;
        [[nodiscard]] QString telemetryGame() const;
        [[nodiscard]] double telemetrySpeed() const;
        [[nodiscard]] int telemetryGear() const;

        [[nodiscard]] QVariantList sessions() const;
        [[nodiscard]] bool sessionsLoading() const;
        [[nodiscard]] QString sessionsError() const;
        [[nodiscard]] QVariantList jobs() const;
        [[nodiscard]] QVariantList saveGameProfiles() const;
        [[nodiscard]] QVariantMap career() const;
        [[nodiscard]] QVariantMap company() const;
        [[nodiscard]] bool careerLoading() const;
        [[nodiscard]] QString careerError() const;

        Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
        /// Names a button of the current wheel; an empty name removes it. Saved at once.
        Q_INVOKABLE void setButtonName(int index, const QString& name);
        /// What a button does in the current profile; an empty note removes it. Saved with the profile.
        Q_INVOKABLE void setButtonNote(int index, const QString& note);
        Q_INVOKABLE void applyMode(const QString& mode);
        Q_INVOKABLE void changeRangeBy(int delta);
        Q_INVOKABLE void refresh();
        /// Writes every setting to the wheel again - a game that opens the wheel resets some of them.
        void applyAll();

        Q_INVOKABLE void loadProfile(const QString& name);
        Q_INVOKABLE void saveProfile();
        Q_INVOKABLE [[nodiscard]] bool profileExists(const QString& name) const;
        Q_INVOKABLE void createProfile(const QString& name);
        Q_INVOKABLE void renameProfile(const QString& from, const QString& to);
        Q_INVOKABLE void deleteProfile(const QString& name);
        Q_INVOKABLE void importProfile(const QUrl& file);
        Q_INVOKABLE void exportProfile(const QString& name, const QUrl& file);

        Q_INVOKABLE void startStopButtonSetup();
        Q_INVOKABLE void stopButtonSetup();
        Q_INVOKABLE void installPermissionRules();
        /// Starts the companion command, unless it is to be started by hand.
        void startCommandUnlessManual();
        Q_INVOKABLE void startCommand();

        /// Reads the recorded drives, jobs and costs on the road from the database in the background;
        /// sessionsChanged() says when they are there.
        Q_INVOKABLE void refreshSessions();

        /// Looks for the profiles again, then reads the save of the one at this index of saveGameProfiles in the
        /// background; careerChanged() says when it is there.
        Q_INVOKABLE void loadCareer(int profile = 0);
        /// Writes the deliveries of the save loadCareer() read that the database does not know yet.
        Q_INVOKABLE void importDeliveries();

        Q_INVOKABLE void startTest();
        Q_INVOKABLE void runTest();
        Q_INVOKABLE void previousTest();
        Q_INVOKABLE void importTestReport(const QUrl& file);
        Q_INVOKABLE void exportTestReport(const QUrl& file);
        Q_INVOKABLE [[nodiscard]] QString defaultReportName() const;
        /// One of the three result charts - 0 linearity, 1 step position and velocity, 2 acceleration - as
        /// {duration, left: {from, to, series: [{name, color, values}]}, right: {...}}, every line sampled at the
        /// same evenly spaced times over `duration` seconds.
        Q_INVOKABLE [[nodiscard]] QVariantMap chartData(int index) const;

    Q_SIGNALS:
        void devicesChanged();
        void deviceChanged();
        void profilesChanged();
        void settingsChanged();
        void buttonNamesChanged();
        void inputChanged();
        void overlayChanged();
        void buttonSetupChanged();
        void preferencesChanged();
        void commandChanged();
        void testChanged();
        void telemetryChanged();
        void sessionsChanged();
        void careerChanged();
        /// The wheel's settings files cannot be written, and the user has not turned the check off.
        void permissionsNeeded(const QString& rulesFile, const QString& targetDirectory);
        void message(const QString& title, const QString& text, bool error);
        void quitRequested();

    private:
        /// What the background read of the database brings back.
        struct LoadedSessions
        {
            QList<Statistics::SessionSummary> sessions;
            QList<Statistics::JobRecord> jobs;
            QList<Statistics::GameEventRecord> events;
            QString error;
        };

        /// What the background read of a save game brings back.
        struct LoadedCareer
        {
            SaveGame::SaveGameProfile profile;
            SaveGame::Career career;
            SaveGame::Company company;
            QList<Statistics::JobRecord> deliveries;
            /// The game's names in the application's language, for what the save game knows by id.
            QHash<QString, QHash<QString, QString>> names;
            /// The repairs the database keeps, the latest first; or, without it (repairsKept false), the save game's.
            QList<Statistics::RepairRecord> repairs;
            bool repairsKept = false;
            QString error;
        };

        struct DeviceState
        {
            Settings::WheelSettings settings;
            Settings::WheelSettings reference;
            QString profile;
        };

        struct ButtonConfig
        {
            QList<int> toggle;
            std::array<int, 8> actions{-1, -1, -1, -1, -1, -1, -1, -1};
        };

        DeviceState& state();
        [[nodiscard]] const DeviceState* currentState() const;
        void notifyDeviceChanges();
        void updateDevices();
        void attachDevice(Device::WheelDevice* device);
        void detachDevice();
        void centerWheel();
        void reloadProfiles();
        void reloadButtonNames();
        static QString buttonNamesPath();
        static QString profilePath(const QString& name);
        static void migrateUpstreamProfiles();

        void storeSession(const Statistics::SessionSummary& summary);
        void onCareerLoaded();
        void updateCareer();
        /// Reads the installed games' names into the database in the background, when their archives changed.
        void refreshGameNames();
        void onGameEvent(const Telemetry::Event& event);
        void onSessionsLoaded();

        void onButton(int index, bool pressed);
        void onSetupButton(int index, bool pressed);
        void onWheelButton(int index, bool pressed);
        void onHat(int axis, int value);
        void setGrabbed(bool grabbed);
        void updateOverlay();
        void readFfbMeter();

        void loadPreferences();
        void savePreferences() const;

        void onTestFinished(bool succeeded, const QString& error);
        void cancelTest();

        Device::DeviceManager* m_deviceManager;
        StartupOptions m_options;
        bool m_initialStateUsed = false;
        QPointer<Device::WheelDevice> m_device;
        QHash<QString, DeviceState> m_states;
        /// The state to carry over to the device that comes back after a mode switch.
        std::optional<DeviceState> m_pendingState;
        QStringList m_profiles;
        /// The names of the current wheel's buttons; read when the wheel is attached.
        Settings::ButtonLabels m_buttonNames;
        /// The ready wheels by id, with their names, as of the last device change - to notify about what changed.
        QHash<QString, QString> m_readyDevices;

        double m_steering = 0;
        double m_throttle = 0;
        double m_brake = 0;
        double m_clutch = 0;
        int m_hatX = 0;
        int m_hatY = 0;
        quint32 m_buttons = 0;

        bool m_grabbed = false;
        int m_ffbMeterLeds = 0;
        QTimer m_ffbMeterTimer;

        Telemetry::TelemetryReceiver m_telemetry;
        Statistics::SessionRecorder m_recorder;
        Statistics::SessionStore m_sessions;
        QFutureWatcher<LoadedSessions> m_sessionsWatcher;
        QVariantList m_sessionList;
        QVariantList m_jobList;
        /// The jobs of the last read, to tell which deliveries of a save game are new.
        QList<Statistics::JobRecord> m_knownJobs;

        // ponytail: looked up once at start; installing the plugin while the application runs needs a restart.
        const bool m_telemetryPluginInstalled = Telemetry::pluginInstalled();

        /// Reads the names of the installed games into the database when their archives changed; true when it wrote.
        QFutureWatcher<bool> m_gameNamesWatcher;

        QList<SaveGame::SaveGameProfile> m_saveGameProfiles;
        QFutureWatcher<LoadedCareer> m_careerWatcher;
        std::optional<LoadedCareer> m_career;
        QVariantMap m_careerMap;
        QVariantMap m_companyMap;
        QString m_careerError;
        QString m_sessionsError;
        Statistics::JobTracker m_jobTracker;
        /// A session was saved while the database was being read: read again when that is done.
        bool m_sessionsStale = false;

        ButtonConfig m_buttonConfig;
        ButtonConfig m_buttonDraft;
        std::optional<int> m_setupStep;
        int m_pressedCount = 0;

        QProcess* m_command = nullptr;

        std::unique_ptr<HardwareTest::FfbTest> m_test;
        TestStage m_testStage = TestStage::Idle;
        /// The prompt the running test was started from, to return to when it fails.
        TestStage m_promptStage = TestStage::Idle;
        std::optional<HardwareTest::TestAnalysis::Report> m_report;
    };
} // namespace Wheelstar::Ui
