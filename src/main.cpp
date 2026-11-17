// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/devicemanager.h"
#include "device/wheeldevice.h"
#include "settings/wheelsettings.h"
#include "telemetry/telemetryreceiver.h"
#include "ui/controller.h"
#include "wheelstarsettings.h"

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KGlobalAccel>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <KNotification>
#include <KStatusNotifierItem>
#include <KWindowSystem>

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QLoggingCategory>
#include <QMenu>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QThread>

#include <memory>
#include <optional>
#include <span>

using namespace Qt::StringLiterals;

using Wheelstar::Device::DeviceManager;
using Wheelstar::Device::WheelDevice;
using Wheelstar::Settings::WheelSettings;
using Wheelstar::Ui::Controller;

namespace
{
    QTextStream& out()
    {
        static QTextStream stream(stdout);
        return stream;
    }

    /// An integer option, or an error for one that is set but not a number.
    std::expected<std::optional<int>, QString> intOption(const QCommandLineParser& parser, const QString& name)
    {
        if (!parser.isSet(name))
        {
            return std::nullopt;
        }

        bool ok = false;
        const int value = parser.value(name).toInt(&ok);

        if (!ok)
        {
            return std::unexpected(i18n("--%1 takes a number", name));
        }

        return value;
    }

    /// --name / --no-name.
    std::optional<bool> switchOption(const QCommandLineParser& parser, const QString& name)
    {
        if (parser.isSet(name))
        {
            return true;
        }

        if (parser.isSet(u"no-"_s + name))
        {
            return false;
        }

        return std::nullopt;
    }

    void addOptions(QCommandLineParser& parser)
    {
        parser.setApplicationDescription(i18n("Wheelstar - Steering Wheel Manager"));
        parser.addHelpOption();
        parser.addVersionOption();

        // Everything after the first positional argument belongs to the command, options included:
        // `istar-wheelstar -p rally -- %command%` hands the game its own flags untouched.
        parser.setOptionsAfterPositionalArgumentsMode(QCommandLineParser::ParseAsPositionalArguments);
        parser.addPositionalArgument(u"command"_s, i18n("Run as command's companion"), u"[command...]"_s);

        parser.addOptions({
            {u"device"_s, i18n("Device path"), u"path"_s},
            {u"list"_s, i18n("list connected devices")},
            {u"mode"_s, i18n("set the compatibility mode"), u"mode"_s},
            {u"range"_s, i18n("set the rotation range [40-900]"), u"degrees"_s},
            {u"combine-pedals"_s, i18n("combine pedals [0-2]"), u"0-2"_s},
            {u"autocenter"_s, i18n("set the autocenter strength [0-100]"), u"0-100"_s},
            {u"ff-gain"_s, i18n("set the FF gain [0-100]"), u"0-100"_s},
            {u"spring-level"_s, i18n("set the spring level [0-100]"), u"0-100"_s},
            {u"damper-level"_s, i18n("set the damper level [0-100]"), u"0-100"_s},
            {u"friction-level"_s, i18n("set the friction level [0-100]"), u"0-100"_s},
            {u"ffb-leds"_s, i18n("enable FFBmeter leds")},
            {u"no-ffb-leds"_s, i18n("disable FFBmeter leds")},
            {u"center-wheel"_s, i18n("center wheel")},
            {u"no-center-wheel"_s, i18n("don't center wheel")},
            {u"start-manually"_s, i18n("run command manually")},
            {u"no-start-manually"_s, i18n("don't run command manually")},
            {{u"p"_s, u"profile"_s}, i18n("load settings from a profile"), u"name"_s},
            {{u"g"_s, u"gui"_s}, i18n("start the GUI")},
            {u"debug"_s, i18n("enable debug output")},
        });
    }

    /// The settings the command line asks for; an error names the option that is not a number.
    std::expected<WheelSettings, QString> readSettings(const QCommandLineParser& parser)
    {
        WheelSettings settings;
        const std::array<std::pair<QString, std::optional<int> WheelSettings::*>, 7> numbers{{
            {u"range"_s, &WheelSettings::range},
            {u"combine-pedals"_s, &WheelSettings::combinePedals},
            {u"autocenter"_s, &WheelSettings::autocenter},
            {u"ff-gain"_s, &WheelSettings::ffGain},
            {u"spring-level"_s, &WheelSettings::springLevel},
            {u"damper-level"_s, &WheelSettings::damperLevel},
            {u"friction-level"_s, &WheelSettings::frictionLevel},
        }};

        for (const auto& [name, member] : numbers)
        {
            const auto value = intOption(parser, name);

            if (!value)
            {
                return std::unexpected(value.error());
            }

            settings.*member = *value;
        }

        if (parser.isSet(u"mode"_s))
        {
            settings.mode = parser.value(u"mode"_s);
        }

        settings.ffbLeds = switchOption(parser, u"ffb-leds"_s);
        settings.centerWheel = switchOption(parser, u"center-wheel"_s);
        return settings;
    }

    /// Upstream's console mode: apply the settings, start the command, exit.
    int runWithoutWindow(const DeviceManager& devices, WheelDevice* device, const QString& profile,
                         WheelSettings settings, const QStringList& command)
    {
        if (device == nullptr)
        {
            out() << i18n("No device available.") << Qt::endl;
            return 1;
        }

        if (!device->hasPermissions())
        {
            out() << i18n("You don't have the required permissions to change your wheel settings.") << Qt::endl;
            return 1;
        }

        Q_UNUSED(devices)
        device->open();

        if (!profile.isEmpty())
        {
            auto loaded = WheelSettings::load(Controller::profileDirectory() + u'/' + profile + ".toml"_L1);

            if (!loaded)
            {
                out() << loaded.error() << Qt::endl;
                return 1;
            }

            loaded->overrideWith(settings);
            settings = *loaded;
        }

        if (settings.mode && settings.mode != device->mode())
        {
            // The wheel reconnects as another device, which the remaining settings would have to wait for.
            device->setMode(*settings.mode);
            out() << i18n("The mode was changed; run the command again for the other settings once the wheel is back.")
                  << Qt::endl;
        }
        else
        {
            if (settings.centerWheel.value_or(false))
            {
                device->setAutocenter(100);
                QThread::sleep(std::chrono::seconds(1));
                device->setAutocenter(0);
            }

            settings.applyTo(*device);
        }

        if (!command.isEmpty())
        {
            const bool started = command.size() == 1 ? QProcess::startDetached(u"/bin/sh"_s, {u"-c"_s, command.first()})
                                                     : QProcess::startDetached(command.first(), command.mid(1));

            if (!started)
            {
                out() << i18n("The supplied command failed:\n%1", command.join(u' ')) << Qt::endl;
                return 1;
            }
        }

        return 0;
    }

    /// The tray icon: the window can be closed while the program keeps running next to a game, a click brings it
    /// back, and the profiles are in the context menu.
    void addTrayIcon(Controller& controller, QQuickWindow* window)
    {
        auto* tray = new KStatusNotifierItem(window);
        tray->setIconByName(u"net.kurokyuu.wheelstar"_s);
        tray->setCategory(KStatusNotifierItem::Hardware);
        tray->setStatus(KStatusNotifierItem::Active);
        tray->setAssociatedWindow(window);

        // ponytail: without a tray host (no Plasma, no SNI watcher) a closed window leaves a process with no way
        // back to it but a second launch; ask the tray whether it is shown if that ever comes up.
        QGuiApplication::setQuitOnLastWindowClosed(false);

        QMenu* profiles = tray->contextMenu()->addMenu(QIcon::fromTheme(u"user-identity"_s), i18n("Profiles"));
        const auto refresh = [&controller, tray, profiles]
        {
            profiles->clear();

            for (const QString& name : controller.profiles())
            {
                // A lone & in a menu text marks the next letter as its shortcut.
                QAction* action = profiles->addAction(QString(name).replace(u'&', "&&"_L1), &controller,
                                                      [&controller, name] { controller.loadProfile(name); });
                action->setCheckable(true);
                action->setChecked(name == controller.currentProfile());
            }

            profiles->setEnabled(controller.hasDevice() && !profiles->isEmpty());
            tray->setToolTip(u"net.kurokyuu.wheelstar"_s, i18n("Wheelstar"), controller.currentProfile());
        };

        // settingsChanged also fires for every slider step; rebuilding a menu of a few profiles is cheap enough.
        QObject::connect(&controller, &Controller::profilesChanged, tray, refresh);
        QObject::connect(&controller, &Controller::settingsChanged, tray, refresh);
        QObject::connect(&controller, &Controller::deviceChanged, tray, refresh);
        refresh();
    }

    /// Shortcuts that work while a game has the focus. They come without keys: which ones are free is the user's
    /// call, in System Settings > Shortcuts. A notification says what happened, as the window is not in view then.
    void addGlobalShortcuts(Controller& controller)
    {
        const auto add = [&controller](const QString& id, const QString& text, auto run)
        {
            auto* action = new QAction(text, &controller);
            action->setObjectName(id);
            QObject::connect(action, &QAction::triggered, &controller,
                             [&controller, run]
                             {
                                 if (controller.hasDevice())
                                 {
                                     run();
                                 }
                             });
            KGlobalAccel::setGlobalShortcut(action, QList<QKeySequence>());
        };

        const auto loadNeighbour = [&controller](qsizetype step)
        {
            const QStringList names = controller.profiles();

            if (names.isEmpty())
            {
                return;
            }

            // With no profile loaded, "next" is the first one and "previous" the last.
            qsizetype index = names.indexOf(controller.currentProfile());
            index = index < 0 && step < 0 ? 0 : index;
            const QString& name = names.at((index + step + names.size()) % names.size());
            controller.loadProfile(name);
            KNotification::event(u"shortcutUsed"_s, i18n("Profile loaded"), name, u"input-gaming"_s);
        };

        // A game that opens the wheel resets some settings (SDL turns autocenter off); this puts them back.
        add(u"apply-settings"_s, i18n("Apply the settings to the wheel again"),
            [&controller]
            {
                controller.applyAll();
                KNotification::event(u"shortcutUsed"_s, i18n("Settings applied"), controller.currentProfile(),
                                     u"input-gaming"_s);
            });
        add(u"next-profile"_s, i18n("Next profile"), [loadNeighbour] { loadNeighbour(1); });
        add(u"previous-profile"_s, i18n("Previous profile"), [loadNeighbour] { loadNeighbour(-1); });
    }
} // namespace

int main(int argc, char* argv[])
{
    // Which application object is needed depends on the arguments, and the arguments can only be read once
    // there is one; so a plain one first, swapped for a GUI one when the window is wanted.
    QStringList arguments;

    // argv is where the C array comes in: its length is argc's word, nothing else can vouch for it. From here on
    // the span carries both together.
#ifdef __clang__
#pragma clang unsafe_buffer_usage begin
#endif
    const std::span argumentVector(argv, static_cast<std::size_t>(argc));
#ifdef __clang__
#pragma clang unsafe_buffer_usage end
#endif

    for (const char* argument : argumentVector)
    {
        arguments.append(QString::fromLocal8Bit(argument));
    }

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("istar-wheelstar"));

    // Read before anything is translated, which is why the settings are opened before the application object.
    if (const QString language = WheelstarSettings::language(); !language.isEmpty())
    {
        KLocalizedString::setLanguages({language});
    }

    QCoreApplication::setApplicationName(u"istar-wheelstar"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(WHEELSTAR_VERSION));

    QCommandLineParser parser;
    addOptions(parser);
    parser.parse(arguments);

    const QStringList command = parser.positionalArguments();
    const bool settingsGiven = std::ranges::any_of(
        parser.optionNames(), [](const QString& name)
        { return name != "device"_L1 && name != "debug"_L1 && name != "g"_L1 && name != "gui"_L1; });
    // Upstream counted argv words, so `--device /dev/input/event16` alone counted as a setting.
    const bool startGui = parser.isSet(u"gui"_s) || (!settingsGiven && command.isEmpty());

    std::unique_ptr<QCoreApplication> application;

    if (startGui && !parser.isSet(u"help"_s) && !parser.isSet(u"version"_s) && !parser.isSet(u"list"_s))
    {
        // Has to be set before the first window exists. QSG_RHI_BACKEND in the environment still overrides it.
        if (!qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
        {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        }

        application = std::make_unique<QApplication>(argc, argv);
    }
    else
    {
        application = std::make_unique<QCoreApplication>(argc, argv);
    }

    KAboutData about(u"istar-wheelstar"_s, i18n("Wheelstar"), QStringLiteral(WHEELSTAR_VERSION),
                     i18n("Steering Wheel Manager"), KAboutLicense::GPL_V3,
                     i18n("© 2019 Bernat Arlandis, © 2026 Pascal Klose"),
                     i18n("A Qt and Kirigami port of Oversteer by Bernat Arlandis."));
    about.addAuthor(i18n("Bernat Arlandis"), i18n("Oversteer"), u"berarma@hotmail.com"_s,
                    u"https://github.com/berarma/oversteer"_s);
    about.addAuthor(i18n("Pascal Klose"), i18n("Qt and Kirigami port"), u"istarlabs_dev@proton.me"_s,
                    u"https://github.com/Istarlabs/istar-wheelstar"_s);
    about.setDesktopFileName(u"net.kurokyuu.wheelstar"_s);
    about.setOrganizationDomain(QByteArrayLiteral("kurokyuu.net"));
    // The default is bugs.kde.org, which is where the crash handler and "Report Bug" would send people.
    about.setBugAddress(QByteArrayLiteral("https://github.com/Istarlabs/istar-wheelstar/issues"));
    about.setProgramLogo(QIcon::fromTheme(u"net.kurokyuu.wheelstar"_s));

    // The plugin is a package of its own and optional; the About page names it among the libraries when it is there.
    // Both packages are built from this source at once, so it has this version.
    if (Wheelstar::Telemetry::pluginInstalled())
    {
        about.addComponent(i18n("Truck Simulator telemetry plugin"),
                           i18n("Sends the telemetry of Euro Truck Simulator 2 and American Truck Simulator to "
                                "Wheelstar (istar-wheelstar-ets2-plugin)"),
                           QStringLiteral(WHEELSTAR_VERSION), u"https://github.com/Istarlabs/istar-wheelstar"_s,
                           KAboutLicense::GPL_V3);
    }

    KAboutData::setApplicationData(about);
    KCrash::initialize();

    parser.process(*application);

    if (parser.isSet(u"debug"_s))
    {
        QLoggingCategory::setFilterRules(u"wheelstar.debug=true"_s);
    }

    const auto settings = readSettings(parser);

    if (!settings)
    {
        out() << settings.error() << Qt::endl;
        return 2;
    }

    // One window per session: a second launch - a game started from Steam with `-p profile -- %command%` while
    // the window is open - hands its arguments to this one and exits here, instead of both driving the wheel.
    std::optional<KDBusService> service;

    if (startGui)
    {
        service.emplace(KDBusService::Unique);
    }

    DeviceManager devices;
    devices.start();

    if (parser.isSet(u"list"_s))
    {
        out() << i18n("Devices found:") << Qt::endl;

        for (const WheelDevice* device : devices.devices())
        {
            out() << u"  %1: %2"_s.arg(device->eventNode(), device->name()) << Qt::endl;
        }

        return 0;
    }

    const QString profile = parser.value(u"profile"_s);

    if (!profile.isEmpty() && !QFile::exists(Controller::profileDirectory() + u'/' + profile + ".toml"_L1))
    {
        out() << i18n("This profile doesn't exist.") << Qt::endl;
        return 1;
    }

    WheelDevice* device = nullptr;

    if (parser.isSet(u"device"_s))
    {
        device = devices.device(QFileInfo(parser.value(u"device"_s)).canonicalFilePath());
    }
    else if (!devices.devices().isEmpty())
    {
        device = devices.devices().first();
    }

    if (!startGui)
    {
        return runWithoutWindow(devices, device, profile, *settings, command);
    }

    // The Breeze look of Qt Quick Controls; a style chosen through the environment is the user's to keep.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
    {
        QQuickStyle::setStyle(u"org.kde.desktop"_s);
    }

    QGuiApplication::setDesktopFileName(u"net.kurokyuu.wheelstar"_s);
    QGuiApplication::setWindowIcon(QIcon::fromTheme(u"net.kurokyuu.wheelstar"_s));

    Controller controller(&devices,
                          {.settings = *settings,
                           .profile = profile,
                           .device = device != nullptr ? device->id() : QString(),
                           .command = command,
                           .startManually = switchOption(parser, u"start-manually"_s)});

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    QObject::connect(&controller, &Controller::quitRequested, application.get(), &QCoreApplication::quit);
    engine.loadFromModule("net.kurokyuu.wheelstar", "Main");

    if (engine.rootObjects().isEmpty())
    {
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    if (window != nullptr)
    {
        addTrayIcon(controller, window);
    }

    addGlobalShortcuts(controller);

    QObject::connect(&*service, &KDBusService::activateRequested, &controller,
                     [&controller, window](const QStringList& arguments, const QString& /*workingDirectory*/)
                     {
                         QCommandLineParser secondParser;
                         addOptions(secondParser);

                         if (secondParser.parse(arguments))
                         {
                             if (const auto requested = readSettings(secondParser))
                             {
                                 controller.applyStartupOptions(
                                     {.settings = *requested,
                                      .profile = secondParser.value(u"profile"_s),
                                      .device = {},
                                      .command = secondParser.positionalArguments(),
                                      .startManually = switchOption(secondParser, u"start-manually"_s)});
                             }
                         }

                         if (window != nullptr)
                         {
                             // On Wayland, raising needs the activation token the second launch passed along.
                             window->show();
                             KWindowSystem::updateStartupId(window);
                             KWindowSystem::activateWindow(window);
                         }
                     });

    controller.startCommandUnlessManual();
    return QCoreApplication::exec();
}
