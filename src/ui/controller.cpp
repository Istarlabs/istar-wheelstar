// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/controller.h"
#include "core/logging.h"
#include "device/devicemanager.h"
#include "device/wheeldevice.h"
#include "hardwaretest/ffbtest.h"
#include "wheelstarsettings.h"

#include <KLocalizedString>
#include <KNotification>

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>

#include <ranges>
#include <utility>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace Wheelstar::Ui
{
    using Device::DeviceManager;
    using Device::WheelDevice;
    using HardwareTest::FfbTest;
    using Settings::WheelSettings;
    namespace TestAnalysis = HardwareTest::TestAnalysis;

    namespace
    {
        // The QML singleton factory has nothing but this to find the one instance main() owns by.
        Controller* s_instance = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

        constexpr auto RulesFile = "99-logitech-wheel-perms.rules"_L1;
        constexpr auto RulesDirectory = "/etc/udev/rules.d"_L1;
        /// The smallest steering range, in degrees, the app offers; the largest is the model's own, see
        /// WheelModel::maxRange. The 40 is the value of the Python Oversteer this is a port of, not measured here.
        constexpr int MinimumRange = 40;

        /// Hat directions count as buttons 100 to 103, so they can be assigned like the others.
        constexpr int HatButtonBase = 100;

        QString configDirectory()
        {
            return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/istar-wheelstar"_L1;
        }

        QString dataDirectory()
        {
            return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/istar-wheelstar"_L1;
        }

        /// The statistics database as the preferences describe it: PostgreSQL when it is asked for, otherwise a
        /// file of our own.
        Statistics::DatabaseConnection databaseConnection()
        {
            Statistics::DatabaseConnection connection;

            if (WheelstarSettings::databaseDriver() == "QPSQL"_L1)
            {
                connection.driver = u"QPSQL"_s;
                connection.host = WheelstarSettings::databaseHost();
                connection.port = WheelstarSettings::databasePort();
                connection.name = WheelstarSettings::databaseName();
                connection.user = WheelstarSettings::databaseUser();
                return connection;
            }

            QDir().mkpath(dataDirectory());
            connection.driver = u"QSQLITE"_s;
            connection.name = dataDirectory() + "/statistics.sqlite"_L1;
            return connection;
        }

        QString gameName(Telemetry::Game game)
        {
            switch (game)
            {
                case Telemetry::Game::EuroTruckSimulator2:
                    return u"Euro Truck Simulator 2"_s;
                case Telemetry::Game::AmericanTruckSimulator:
                    return u"American Truck Simulator"_s;
                case Telemetry::Game::Unknown:
                    break;
            }

            return i18n("Unknown game");
        }

        QString gearLabel(int gear)
        {
            if (gear < 0)
            {
                return u"R"_s;
            }

            return gear == 0 ? u"N"_s : QString::number(gear);
        }

        /// The currency the game pays in.
        QString currency(Telemetry::Game game)
        {
            return game == Telemetry::Game::AmericanTruckSimulator ? u"$"_s : u"€"_s;
        }

        /// The language the game's names are wanted in, as QLocale names it ("de_DE"): the application's own, chosen
        /// in the preferences, else the system's.
        QString gameLanguage()
        {
            const QString setting = WheelstarSettings::language();
            return (setting.isEmpty() ? QLocale::system() : QLocale(setting)).name();
        }

        /// An id of the game as a name, for what the telemetry never named: "smokd_sprats" is "Smokd sprats".
        QString nameFromId(const QString& id)
        {
            QString name = id;
            name.replace(u'_', u' ');

            if (!name.isEmpty())
            {
                name[0] = name[0].toUpper();
            }

            return name;
        }

        /// A vehicle's definition as a name: "scania.r_2016" is "Scania R 2016", the brand and the model.
        QString vehicleName(const QString& model)
        {
            if (model.isEmpty())
            {
                return {};
            }

            return nameFromId(model.section(u'.', 0, 0)) + u' ' + nameFromId(model.section(u'.', 1));
        }

        /// An offence as the SDK and the save game name it, in words.
        QString offenceName(const QString& offence)
        {
            // The order of the save game's counters, which is the SDK's order of fine_offence too.
            static const QHash<QString, KLocalizedString> Names = {
                {u"crash"_s, ki18nc("@item offence", "Crash")},
                {u"avoid_sleeping"_s, ki18nc("@item offence", "No rest when tired")},
                {u"wrong_way"_s, ki18nc("@item offence", "Wrong way")},
                {u"speeding_camera"_s, ki18nc("@item offence", "Speed camera")},
                {u"no_lights"_s, ki18nc("@item offence", "No lights")},
                {u"red_signal"_s, ki18nc("@item offence", "Red light")},
                {u"speeding"_s, ki18nc("@item offence", "Speeding")},
                {u"avoid_weighing"_s, ki18nc("@item offence", "Avoided the weigh station")},
                {u"illegal_trailer"_s, ki18nc("@item offence", "Illegal trailer")},
                {u"avoid_inspection"_s, ki18nc("@item offence", "Avoided an inspection")},
                {u"illegal_border_crossing"_s, ki18nc("@item offence", "Illegal border crossing")},
                {u"hard_shoulder_violation"_s, ki18nc("@item offence", "Driving on the hard shoulder")},
                {u"damaged_vehicle_usage"_s, ki18nc("@item offence", "Driving a damaged vehicle")},
                {u"generic"_s, ki18nc("@item offence", "Other offence")},
            };

            const auto found = Names.constFind(offence);
            return found != Names.cend() ? found->toString() : nameFromId(offence);
        }

        /// A job market as the save game and the telemetry name it, in words.
        QString marketName(const QString& market)
        {
            // The delivery log's short names, the telemetry's long ones, and the bus job log's types.
            static const QHash<QString, KLocalizedString> Names = {
                {u"quick"_s, ki18nc("@item job market", "Quick job")},
                {u"quick_job"_s, ki18nc("@item job market", "Quick job")},
                {u"compn"_s, ki18nc("@item job market", "Freight market")},
                {u"freight_market"_s, ki18nc("@item job market", "Freight market")},
                {u"cargo"_s, ki18nc("@item job market", "Cargo market")},
                {u"cargo_market"_s, ki18nc("@item job market", "Cargo market")},
                {u"on_compn"_s, ki18nc("@item job market", "External contract")},
                {u"external_contracts"_s, ki18nc("@item job market", "External contract")},
                {u"spec_oversize"_s, ki18nc("@item job market", "Special transport")},
                {u"company_job"_s, ki18nc("@item job market", "Company job")},
            };

            const auto found = Names.constFind(market);
            return found != Names.cend() ? found->toString() : nameFromId(market);
        }

        /// What the game's names are for its ids, as far as the telemetry told them: a job from the save game only
        /// has ids, and borrows the names another job brought.
        struct GameNames
        {
            QHash<QString, QString> cities;
            QHash<QString, QString> companies;
            QHash<QString, QString> cargoes;

            explicit GameNames(const QList<Statistics::JobRecord>& jobs)
            {
                const auto learn = [](QHash<QString, QString>& names, const QString& id, const QString& name)
                {
                    if (!id.isEmpty() && !name.isEmpty())
                    {
                        names.insert(id, name);
                    }
                };

                for (const Statistics::JobRecord& job : jobs)
                {
                    learn(cities, job.sourceCityId, job.sourceCity);
                    learn(cities, job.destinationCityId, job.destinationCity);
                    learn(companies, job.sourceCompanyId, job.sourceCompany);
                    learn(companies, job.destinationCompanyId, job.destinationCompany);
                    learn(cargoes, job.cargoId, job.cargo);
                }
            }

            static QString name(const QHash<QString, QString>& names, const QString& name, const QString& id)
            {
                return !name.isEmpty() ? name : names.value(id, nameFromId(id));
            }
        };

        QVariantMap jobToVariant(const Statistics::JobRecord& job, const GameNames& names)
        {
            // In-game minutes since the first day: 1440 to a day, the first day being day 1.
            const quint32 day = (job.gameTime / 1440) + 1;

            return {
                {u"game"_s, gameName(job.game)},
                {u"bus"_s, job.vehicle == Statistics::JobRecord::Vehicle::Bus},
                {u"gameTime"_s, job.gameTime},
                {u"passengers"_s, job.passengers},
                {u"stops"_s, job.stops},
                {u"satisfaction"_s, job.satisfaction},
                {u"fromSaveGame"_s, job.origin == Statistics::JobRecord::Origin::SaveGame},
                {u"delivered"_s, job.delivered},
                {u"finishedAt"_s, job.finishedAt.isValid() ? QVariant(job.finishedAt.toLocalTime()) : QVariant()},
                {u"gameDay"_s, day},
                {u"gameClock"_s, QTime(0, 0).addSecs(static_cast<int>(job.gameTime % 1440) * 60)},
                {u"from"_s, GameNames::name(names.cities, job.sourceCity, job.sourceCityId)},
                {u"fromCompany"_s, GameNames::name(names.companies, job.sourceCompany, job.sourceCompanyId)},
                {u"to"_s, GameNames::name(names.cities, job.destinationCity, job.destinationCityId)},
                {u"toCompany"_s, GameNames::name(names.companies, job.destinationCompany, job.destinationCompanyId)},
                {u"cargo"_s, GameNames::name(names.cargoes, job.cargo, job.cargoId)},
                {u"market"_s, marketName(job.market)},
                // The truck or bus as the save game names it, "vehicle.volvo.fh_2024".
                {u"truck"_s, vehicleName(job.truck.section(u'.', 1))},
                // Kilograms to tonnes.
                {u"tonnes"_s, job.mass / 1000.0},
                {u"plannedKilometres"_s, job.plannedKilometres},
                {u"drivenKilometres"_s, job.drivenKilometres},
                {u"revenue"_s, static_cast<double>(job.revenue)},
                {u"fines"_s, static_cast<double>(job.fines)},
                {u"currency"_s, currency(job.game)},
                {u"experience"_s, job.experience},
                {u"cargoDamage"_s, job.cargoDamage},
                {u"deliveryMinutes"_s, job.deliveryMinutes},
                {u"autoParked"_s, job.autoParked},
                {u"autoLoaded"_s, job.autoLoaded},
                {u"special"_s, job.special},
            };
        }

        QVariantMap eventToVariant(const Statistics::GameEventRecord& event)
        {
            using Kind = Statistics::GameEventRecord::Kind;
            QString what;

            switch (event.kind)
            {
                case Kind::Fine:
                    // i18n: %1 is the offence, "Red light"
                    what = i18n("Fine: %1", offenceName(event.detail));
                    break;
                case Kind::Toll:
                    what = i18n("Toll");
                    break;
                case Kind::Ferry:
                    // i18n: %1 and %2 are the ports
                    what = i18n("Ferry %1 → %2", event.from, event.to);
                    break;
                case Kind::Train:
                    // i18n: %1 and %2 are the stations
                    what = i18n("Train %1 → %2", event.from, event.to);
                    break;
            }

            return {
                {u"what"_s, what},
                {u"at"_s, event.occurredAt.toLocalTime()},
                {u"amount"_s, static_cast<double>(event.amount)},
                {u"currency"_s, currency(event.game)},
            };
        }

        /// One drive as QML reads it: the shares are fractions of the moving time, or of the driving time for the
        /// gears; the speeds are kilometres per hour. `events` are the fines, tolls and crossings, newest first, of
        /// which those during the drive are listed with it.
        QVariantMap sessionToVariant(const Statistics::SessionSummary& session,
                                     const QList<Statistics::GameEventRecord>& events)
        {
            QVariantList during;

            for (const Statistics::GameEventRecord& event : events)
            {
                if (event.game == session.game && event.occurredAt >= session.startedAt &&
                    event.occurredAt <= session.endedAt)
                {
                    during.prepend(eventToVariant(event));
                }
            }

            const double moving = session.movingSeconds;
            // A part of a whole as a fraction from 0 to 1 (the page multiplies by 100 for percent); 0 for an empty
            // whole instead of a division by zero.
            const auto share = [](double seconds, double whole) { return whole > 0 ? seconds / whole : 0.0; };

            QVariantList steering;

            for (const double seconds : session.steeringSeconds)
            {
                steering.append(share(seconds, moving));
            }

            QVariantList gears;

            for (const auto [gear, seconds] : session.gearSeconds.asKeyValueRange())
            {
                gears.append(QVariantMap{{u"label"_s, gearLabel(gear)},
                                         {u"fraction"_s, share(seconds, session.drivingSeconds)}});
            }

            // The protocol 3 figures, or nothing (null in QML) for a drive recorded before them.
            QVariantMap details;

            if (session.detailed)
            {
                const double gameKilometres = session.gameDistanceMetres.value_or(0) / 1000.0;
                details = {
                    {u"speeding"_s, share(session.speedingSeconds, moving)},
                    {u"speedingDistance"_s, share(session.speedingMetres, session.distanceMetres)},
                    // m/s to km/h, as for the speeds below.
                    {u"maxOverLimitKmh"_s, session.maxOverLimit * 3.6},
                    {u"cruiseControl"_s, share(session.cruiseSeconds, moving)},
                    {u"retarder"_s, share(session.retarderSeconds, moving)},
                    {u"motorBrake"_s, share(session.motorBrakeSeconds, moving)},
                    {u"fuelLitres"_s, session.fuelLitres},
                    // Litres per 100 of the game's kilometres, the way the truck's own computer shows consumption.
                    {u"fuelPer100Km"_s,
                     gameKilometres > 0 ? QVariant(session.fuelLitres / gameKilometres * 100.0) : QVariant()},
                    {u"truckWear"_s, session.truckWear},
                    {u"cargoDamage"_s, session.cargoDamage},
                };
            }

            return {
                {u"events"_s, during},
                {u"details"_s, session.detailed ? QVariant(details) : QVariant()},
                {u"game"_s, gameName(session.game)},
                {u"startedAt"_s, session.startedAt.toLocalTime()},
                {u"endedAt"_s, session.endedAt.toLocalTime()},
                {u"drivingSeconds"_s, session.drivingSeconds},
                {u"movingSeconds"_s, moving},
                // The distance as the game shows it, in kilometres (1000 metres each); null for a drive recorded
                // before the plugin sent the odometer.
                {u"distanceKilometres"_s,
                 session.gameDistanceMetres ? QVariant(*session.gameDistanceMetres / 1000.0) : QVariant()},
                // The game speaks metres per second. 3.6 is 3600 seconds to the hour over 1000 metres to the
                // kilometre, so 10 m/s is 36 km/h.
                {u"averageKmh"_s, session.averageSpeed() * 3.6},
                {u"maxKmh"_s, session.maxSpeed * 3.6},
                {u"corrections"_s, session.steeringCorrections},
                // Corrections per minute of moving time: the count over the moving seconds turned into minutes.
                {u"correctionsPerMinute"_s, moving > 0 ? session.steeringCorrections / (moving / 60.0) : 0.0},
                {u"hardBrakes"_s, session.hardBrakes},
                {u"throttleIdle"_s, share(session.throttleIdleSeconds, moving)},
                {u"throttleFull"_s, share(session.throttleFullSeconds, moving)},
                {u"brake"_s, share(session.brakeSeconds, moving)},
                {u"steering"_s, steering},
                {u"gears"_s, gears},
            };
        }

        QStringList buttonSetupLabels()
        {
            return {
                i18n("Press toggle button/s"),    i18n("Press button to set 270°"), i18n("Press button to set 360°"),
                i18n("Press button to set 540°"), i18n("Press button to set 900°"), i18n("Press button for +10°"),
                i18n("Press button for -10°"),    i18n("Press button for +90°"),    i18n("Press button for -90°"),
            };
        }

        /// What each assignable button does once the wheel is grabbed: set a range, or change it by some degrees.
        constexpr std::array<std::pair<int, bool>, 8> ButtonActions{{
            {270, false},
            {360, false},
            {540, false},
            {900, false},
            {10, true},
            {-10, true},
            {90, true},
            {-90, true},
        }};

        /// What a chart shows at most per line. The series are sampled every millisecond, 15 000 points for the
        /// linearity test, which the chart would draw no better and much slower.
        constexpr int ChartPoints = 1000;

        struct ChartLine
        {
            const TestAnalysis::Series* points;
            const char* color;
            QString name;
        };

        /// The value at `time`, interpolated; `cursor` remembers where the previous lookup ended.
        double valueAt(const TestAnalysis::Series& points, double time, qsizetype* cursor)
        {
            while (*cursor + 1 < points.size() && points.at(*cursor + 1).x() <= time)
            {
                ++*cursor;
            }

            const QPointF& before = points.at(*cursor);

            if (*cursor + 1 >= points.size() || time <= before.x())
            {
                return before.y();
            }

            const QPointF& after = points.at(*cursor + 1);
            return before.y() + ((after.y() - before.y()) * (time - before.x()) / (after.x() - before.x()));
        }

        /// One axis of a chart: its lines, all sampled at the same times, and the value range they span.
        QVariantMap chartAxis(const QList<ChartLine>& lines, double duration, double* minimum, double* maximum)
        {
            QVariantList series;

            for (const ChartLine& line : lines)
            {
                QVariantList values;
                values.reserve(ChartPoints);
                qsizetype cursor = 0;

                for (int index = 0; index < ChartPoints && !line.points->isEmpty(); ++index)
                {
                    const double value = valueAt(*line.points, duration * index / (ChartPoints - 1), &cursor);
                    *minimum = std::min(*minimum, value);
                    *maximum = std::max(*maximum, value);
                    values.append(value);
                }

                series.append(QVariantMap{{u"name"_s, line.name},
                                          {u"color"_s, QColor(QLatin1StringView(line.color))},
                                          {u"values"_s, values}});
            }

            return {{u"series"_s, series}};
        }

        /// Both axes, with their zeros at the same height as upstream's charts had them.
        QVariantMap chart(const QList<ChartLine>& left, const QList<ChartLine>& right)
        {
            double duration = 0;

            for (const auto& line : left + right)
            {
                if (!line.points->isEmpty())
                {
                    duration = std::max(duration, line.points->last().x());
                }
            }

            double leftMinimum = 0;
            double leftMaximum = 0;
            double rightMinimum = 0;
            double rightMaximum = 0;
            QVariantMap leftAxis = chartAxis(left, duration, &leftMinimum, &leftMaximum);
            QVariantMap rightAxis = chartAxis(right, duration, &rightMinimum, &rightMaximum);

            // Where zero sits, as a fraction from the bottom; the lower of the two is moved up to the other.
            const auto zeroAt = [](double minimum, double maximum)
            { return maximum > minimum ? -minimum / (maximum - minimum) : 0.0; };
            const double zero = std::max(zeroAt(leftMinimum, leftMaximum), zeroAt(rightMinimum, rightMaximum));

            for (auto [minimum, maximum] : {std::pair{&leftMinimum, &leftMaximum}, {&rightMinimum, &rightMaximum}})
            {
                if (*maximum <= *minimum)
                {
                    *maximum = *minimum + 1;
                }

                if (zero > 0 && zero < 1)
                {
                    *minimum = std::min(*minimum, -zero * *maximum / (1 - zero));
                }
            }

            leftAxis.insert(u"from"_s, leftMinimum);
            leftAxis.insert(u"to"_s, leftMaximum);
            rightAxis.insert(u"from"_s, rightMinimum);
            rightAxis.insert(u"to"_s, rightMaximum);

            return {{u"duration"_s, duration}, {u"left"_s, leftAxis}, {u"right"_s, rightAxis}};
        }

        /// The money a profit log comes to: what came in, and what went out (wages, upkeep, fuel).
        std::pair<qint64, qint64> moneyOf(const QList<SaveGame::ProfitEntry>& log)
        {
            qint64 revenue = 0;
            qint64 costs = 0;

            for (const SaveGame::ProfitEntry& entry : log)
            {
                revenue += entry.revenue;
                costs += entry.wage + entry.maintenance + entry.fuel;
            }

            return {revenue, costs};
        }

        /// The company for QML. The save game knows cities, companies and cargoes by id only; `names` has the game's
        /// names in the application's language, and an id it does not know is made a name of. `repairs` are those the
        /// database keeps (`repairsKept`), else the save game's own last days; the latest first either way.
        QVariantMap companyToVariant(const SaveGame::Company& company,
                                     const QHash<QString, QHash<QString, QString>>& names,
                                     const QList<Statistics::RepairRecord>& repairs, bool repairsKept)
        {
            const auto named = [&names](const QString& kind, const QString& id)
            { return id.isEmpty() ? QString() : names.value(kind).value(id, nameFromId(id)); };
            // A save game has no names of drivers, only their numbers.
            const auto driverName = [&company](const QString& id)
            {
                return id == company.playerId ? i18nc("@item the player among the drivers", "You")
                                              : i18n("Driver %1", id.section(u'.', 1));
            };
            QHash<QString, const SaveGame::Truck*> trucks;

            for (const SaveGame::Truck& truck : company.trucks)
            {
                trucks.insert(truck.id, &truck);
            }

            const auto truckName = [&trucks](const QString& id)
            {
                const SaveGame::Truck* truck = trucks.value(id);
                return truck == nullptr ? QString() : vehicleName(truck->model);
            };

            QVariantList garages;
            qint64 revenue = 0;
            qint64 costs = 0;

            for (const TruckData::SaveGame::Garage& garage : company.garages)
            {
                const auto [garageRevenue, garageCosts] = moneyOf(garage.log);
                revenue += garageRevenue;
                costs += garageCosts;
                garages.append(QVariantMap{
                    {u"city"_s, named(u"city"_s, garage.cityId)},
                    {u"headquarters"_s, garage.cityId == company.headquartersCityId},
                    {u"slots"_s, garage.slotCount},
                    {u"trucks"_s, garage.truckIds.size()},
                    {u"drivers"_s, garage.driverIds.size()},
                    {u"trailers"_s, garage.trailers},
                    {u"productivity"_s, garage.productivity},
                    {u"revenue"_s, static_cast<double>(garageRevenue)},
                    {u"costs"_s, static_cast<double>(garageCosts)},
                });
            }

            // The headquarters first, then by city.
            std::ranges::sort(garages,
                              [](const QVariant& left, const QVariant& right)
                              {
                                  const QVariantMap a = left.toMap();
                                  const QVariantMap b = right.toMap();

                                  if (a.value(u"headquarters"_s) != b.value(u"headquarters"_s))
                                  {
                                      return a.value(u"headquarters"_s).toBool();
                                  }

                                  return QString::localeAwareCompare(a.value(u"city"_s).toString(),
                                                                     b.value(u"city"_s).toString()) < 0;
                              });

            QVariantList drivers;

            for (const TruckData::SaveGame::Driver& driver : company.drivers)
            {
                QVariantMap job;

                if (driver.job)
                {
                    job = {
                        {u"cargo"_s, named(u"cargo"_s, driver.job->cargoId)},
                        {u"from"_s, named(u"city"_s, driver.job->sourceCityId)},
                        {u"to"_s, named(u"city"_s, driver.job->destinationCityId)},
                        {u"kilometres"_s, driver.job->plannedKilometres},
                    };
                }

                const auto [driverRevenue, driverCosts] = moneyOf(driver.log);
                drivers.append(QVariantMap{
                    {u"name"_s, driverName(driver.id)},
                    {u"garage"_s, named(u"city"_s, driver.garageCityId)},
                    {u"hometown"_s, named(u"city"_s, driver.hometownId)},
                    {u"city"_s, named(u"city"_s, driver.currentCityId)},
                    {u"experience"_s, driver.experience},
                    {u"skills"_s,
                     QVariantList{driver.adr, driver.longDistance, driver.highValue, driver.fragile, driver.justInTime,
                                  driver.ecoDriving}},
                    {u"job"_s, job},
                    {u"truck"_s, truckName(driver.truckId)},
                    {u"revenue"_s, static_cast<double>(driverRevenue)},
                    {u"costs"_s, static_cast<double>(driverCosts)},
                });
            }

            // A truck or a bus: the save game keeps both alike.
            const auto motorVehicle = [&named, &driverName](const SaveGame::Truck& vehicle)
            {
                return QVariantMap{
                    {u"name"_s, vehicleName(vehicle.model)},
                    {u"plate"_s, vehicle.licensePlate},
                    {u"kilometres"_s, vehicle.odometer},
                    // The worst part is what a repair is about.
                    {u"wear"_s,
                     std::max({vehicle.engineWear, vehicle.transmissionWear, vehicle.cabinWear, vehicle.chassisWear,
                               vehicle.wheelsWear})},
                    {u"fuel"_s, vehicle.fuel},
                    {u"garage"_s, named(u"city"_s, vehicle.garageCityId)},
                    {u"driver"_s, vehicle.driverId.isEmpty() ? QString() : driverName(vehicle.driverId)},
                };
            };
            QVariantList truckList;
            QVariantList busList;
            QVariantList trailerList;

            for (const SaveGame::Truck& truck : company.trucks)
            {
                truckList.append(motorVehicle(truck));
            }

            for (const SaveGame::Truck& bus : company.buses)
            {
                busList.append(motorVehicle(bus));
            }

            for (const TruckData::SaveGame::Trailer& trailer : company.trailers)
            {
                trailerList.append(QVariantMap{
                    {u"name"_s, vehicleName(trailer.model)},
                    {u"plate"_s, nameFromId(trailer.bodyType)},
                    {u"kilometres"_s, trailer.odometer},
                    {u"wear"_s, trailer.bodyWear},
                    {u"garage"_s, named(u"city"_s, trailer.garageCityId)},
                });
            }

            QVariantList repairList;
            qint64 repairsTotal = 0;
            // The first day one was kept on: the latest first, so the last of the list.
            const qint64 repairsSince = repairs.isEmpty() ? 0 : repairs.last().day;

            for (const Statistics::RepairRecord& repair : repairs)
            {
                repairsTotal += repair.amount;
                repairList.append(
                    QVariantMap{{u"day"_s, repair.day}, {u"amount"_s, static_cast<double>(repair.amount)}});
            }

            const auto [ownRevenue, ownCosts] = moneyOf(company.playerLog);

            return {
                {u"headquarters"_s, named(u"city"_s, company.headquartersCityId)},
                {u"garages"_s, garages},
                {u"drivers"_s, drivers},
                {u"trucks"_s, truckList},
                {u"buses"_s, busList},
                {u"trailers"_s, trailerList},
                {u"revenue"_s, static_cast<double>(revenue)},
                {u"costs"_s, static_cast<double>(costs)},
                {u"ownRevenue"_s, static_cast<double>(ownRevenue)},
                {u"ownCosts"_s, static_cast<double>(ownCosts)},
                {u"repairs"_s, repairList},
                {u"repairsTotal"_s, static_cast<double>(repairsTotal)},
                {u"repairsSince"_s, repairsSince},
                {u"repairsKept"_s, repairsKept},
                {u"bank"_s,
                 QVariantMap{
                     {u"money"_s, static_cast<double>(company.bank.money)},
                     {u"loanLimit"_s, static_cast<double>(company.bank.loanLimit)},
                     {u"loans"_s, company.bank.loans},
                     {u"coinsuranceFixed"_s, static_cast<double>(company.bank.coinsuranceFixed)},
                     {u"coinsuranceRatio"_s, company.bank.coinsuranceRatio},
                     {u"overdraft"_s, company.bank.overdraft},
                 }},
            };
        }
    } // namespace

    Controller::Controller(DeviceManager* devices, StartupOptions options, QObject* parent)
        : QObject(parent), m_deviceManager(devices), m_options(std::move(options)),
          m_sessions(databaseConnection(), dataDirectory() + "/pending-sessions"_L1)
    {
        s_instance = this;

        loadPreferences();
        migrateUpstreamProfiles();
        reloadProfiles();

        m_ffbMeterTimer.setInterval(250ms);
        connect(&m_ffbMeterTimer, &QTimer::timeout, this, &Controller::readFfbMeter);
        connect(m_deviceManager, &DeviceManager::devicesChanged, this, &Controller::updateDevices);

        connect(&m_telemetry, &Telemetry::TelemetryReceiver::changed, this, &Controller::telemetryChanged);
        connect(&m_telemetry, &Telemetry::TelemetryReceiver::packetReceived, &m_recorder,
                &Statistics::SessionRecorder::add);
        connect(&m_recorder, &Statistics::SessionRecorder::sessionFinished, this, &Controller::storeSession);
        connect(&m_telemetry, &Telemetry::TelemetryReceiver::eventReceived, this, &Controller::onGameEvent);
        connect(&m_careerWatcher, &QFutureWatcher<LoadedCareer>::finished, this, &Controller::onCareerLoaded);
        connect(&m_exportWatcher, &QFutureWatcher<QString>::finished, this,
                [this]
                {
                    const QString error = m_exportWatcher.result();

                    if (error.isEmpty())
                    {
                        Q_EMIT message(i18n("Save game exported"), m_exportTarget, false);
                    }
                    else
                    {
                        Q_EMIT message(i18n("Error exporting the save game"), error, true);
                    }
                });
        connect(&m_sessionsWatcher, &QFutureWatcher<LoadedSessions>::finished, this, &Controller::onSessionsLoaded);
        connect(&m_gameNamesWatcher, &QFutureWatcher<bool>::finished, this,
                [this]
                {
                    if (m_gameNamesWatcher.result())
                    {
                        refreshSessions();
                    }
                });
        m_telemetry.start();

        // What could not be written last time, when the database was out of reach.
        m_sessions.flushPending();
        refreshGameNames();

        // What is plugged in at start-up is no news either.
        for (const WheelDevice* device : m_deviceManager->devices())
        {
            m_readyDevices.insert(device->id(), device->name());
        }

        WheelDevice* first = m_options.device.isEmpty() ? nullptr : m_deviceManager->device(m_options.device);

        if (first == nullptr && !m_deviceManager->devices().isEmpty())
        {
            first = m_deviceManager->devices().first();
        }

        if (first != nullptr)
        {
            attachDevice(first);
        }
    }

    Controller::~Controller()
    {
        cancelTest();
        detachDevice();
        s_instance = nullptr;
    }

    void Controller::storeSession(const Statistics::SessionSummary& summary)
    {
        // The write is one INSERT, and a server that does not answer is given up on after a few seconds.
        if (m_sessions.save(summary))
        {
            qCInfo(WHEELSTAR) << "Saved the session of" << summary.startedAt << "," << summary.drivingSeconds << "s";
            refreshSessions();
        }
        else
        {
            qCWarning(WHEELSTAR) << "The session could not be saved now; it is kept and tried again later";
        }
    }

    void Controller::onGameEvent(const Telemetry::Event& event)
    {
        const auto record = m_jobTracker.add(event, QDateTime::currentDateTimeUtc());
        bool saved = false;

        if (const auto* job = std::get_if<Statistics::JobRecord>(&record))
        {
            saved = m_sessions.saveJob(*job);
        }
        else if (const auto* happened = std::get_if<Statistics::GameEventRecord>(&record))
        {
            saved = m_sessions.saveEvent(*happened);
        }

        if (saved)
        {
            refreshSessions();
        }
    }

    QVariantList Controller::sessions() const
    {
        return m_sessionList;
    }

    QVariantList Controller::jobs() const
    {
        return m_jobList;
    }

    QVariantList Controller::saveGameProfiles() const
    {
        QVariantList result;

        for (const SaveGame::SaveGameProfile& profile : m_saveGameProfiles)
        {
            result.append(QVariantMap{
                {u"name"_s, profile.name},
                {u"game"_s, gameName(SaveGame::telemetryGame(profile.game))},
                {u"savedAt"_s, profile.savedAt},
            });
        }

        return result;
    }

    QVariantMap Controller::career() const
    {
        return m_careerMap;
    }

    QVariantMap Controller::company() const
    {
        return m_companyMap;
    }

    bool Controller::careerLoading() const
    {
        return m_careerWatcher.isRunning();
    }

    QString Controller::careerError() const
    {
        return m_careerError;
    }

    void Controller::loadCareer(int profile)
    {
        if (m_careerWatcher.isRunning())
        {
            return;
        }

        m_saveGameProfiles = SaveGame::findProfiles();

        if (profile < 0 || profile >= m_saveGameProfiles.size())
        {
            m_career.reset();
            m_careerError = m_saveGameProfiles.isEmpty()
                ? i18n("No save game of Euro Truck Simulator 2 or American Truck Simulator was found.")
                : QString();
            updateCareer();
            return;
        }

        const SaveGame::SaveGameProfile chosen = m_saveGameProfiles.at(profile);
        // The store opens its own connection for every call, so it can be read from another thread.
        const Statistics::SessionStore store = m_sessions;
        const QString language = gameLanguage();
        m_careerWatcher.setFuture(QtConcurrent::run(
            [chosen, store, language]
            {
                LoadedCareer loaded;
                loaded.profile = chosen;
                QFile file(chosen.newestSave);

                if (!file.open(QIODevice::ReadOnly))
                {
                    loaded.error = i18n("Cannot read \"%1\": %2", chosen.newestSave, file.errorString());
                    return loaded;
                }

                // Not readAll(): a file over the library's limit is refused before it is read.
                const auto document = SaveGame::readSiiFile(file);

                if (!document)
                {
                    loaded.error = SaveGame::errorText(document.error());
                    return loaded;
                }

                loaded.career = SaveGame::careerFrom(*document);
                loaded.deliveries = SaveGame::deliveriesFrom(*document, SaveGame::telemetryGame(chosen.game));
                loaded.company = SaveGame::companyFrom(*document);
                // Without the database the company is still shown, its places by their ids.
                loaded.names = store.gameNames(SaveGame::telemetryGame(chosen.game), language)
                                   .value_or(QHash<QString, QHash<QString, QString>>{});

                // The save game remembers the repairs of a few in-game days; the database keeps them. Without it, the
                // save game's last days are all there is.
                const Telemetry::Game game = SaveGame::telemetryGame(chosen.game);
                const QList<Statistics::RepairRecord> seen = SaveGame::repairsFrom(loaded.company, game);

                if (store.writeRepairs(seen) >= 0)
                {
                    if (auto kept = store.repairs(game))
                    {
                        loaded.repairs = std::move(*kept);
                        loaded.repairsKept = true;
                    }
                }

                if (!loaded.repairsKept)
                {
                    loaded.repairs = seen;
                    std::ranges::reverse(loaded.repairs);
                }
                return loaded;
            }));
        Q_EMIT careerChanged();
    }

    void Controller::onCareerLoaded()
    {
        LoadedCareer loaded = m_careerWatcher.result();
        m_careerError = loaded.error;

        if (loaded.error.isEmpty())
        {
            m_career = std::move(loaded);
        }
        else
        {
            m_career.reset();
        }

        updateCareer();
    }

    void Controller::updateCareer()
    {
        m_careerMap.clear();
        m_companyMap.clear();

        if (m_career)
        {
            const SaveGame::Career& career = m_career->career;
            QVariantList offences;

            for (qsizetype i = 0; i < std::ssize(SaveGame::Career::Offences); ++i)
            {
                const qint64 committed = career.offencesCommitted.value(i);
                const qint64 fined = career.offencesFined.value(i);

                // An offence never committed is not worth a row.
                if (committed == 0 && fined == 0)
                {
                    continue;
                }

                offences.append(QVariantMap{
                    {u"name"_s, offenceName(SaveGame::Career::Offences.at(static_cast<std::size_t>(i)))},
                    {u"committed"_s, committed},
                    {u"fined"_s, fined},
                    {u"paid"_s, static_cast<double>(career.finesPaid.value(i))},
                });
            }

            // The minutes of the "by mode" counters to hours, and the game's minutes to days: 60 and 1440.
            const auto hours = [](qint64 minutes) { return static_cast<double>(minutes) / 60.0; };

            m_careerMap = {
                {u"profile"_s, m_career->profile.name},
                {u"game"_s, gameName(SaveGame::telemetryGame(m_career->profile.game))},
                {u"savedAt"_s, m_career->profile.savedAt},
                {u"currency"_s, currency(SaveGame::telemetryGame(m_career->profile.game))},
                {u"experience"_s, career.experience},
                {u"money"_s, static_cast<double>(career.money)},
                {u"gameDays"_s, (career.gameTime / 1440) + 1},
                {u"truckKilometres"_s, career.kilometres.at(0)},
                {u"busKilometres"_s, career.kilometres.at(1)},
                {u"truckHours"_s, hours(career.realMinutes.at(0))},
                {u"busHours"_s, hours(career.realMinutes.at(1))},
                {u"deliveries"_s, career.deliveries},
                {u"busJobs"_s, career.busJobs},
                {u"cancelledJobs"_s, career.cancelledJobs},
                {u"visitedCities"_s, career.visitedCities},
                {u"garages"_s, career.garages},
                {u"ferryCrossings"_s, career.ferryCrossings},
                {u"fuelLitres"_s, career.fuelLitres},
                {u"fuelPrice"_s, static_cast<double>(career.fuelPrice)},
                {u"gasStationVisits"_s, career.gasStationVisits},
                {u"serviceVisits"_s, career.serviceVisits},
                {u"longestClean"_s, career.longestClean},
                {u"currentClean"_s, career.currentClean},
                {u"longestWithoutCargoDamage"_s, career.longestWithoutCargoDamage},
                {u"longestWithoutOffence"_s, career.longestWithoutOffence},
                {u"currentWithoutOffence"_s, career.currentWithoutOffence},
                {u"offences"_s, offences},
                {u"newDeliveries"_s, SaveGame::newDeliveries(m_career->deliveries, m_knownJobs).size()},
            };
            m_companyMap =
                companyToVariant(m_career->company, m_career->names, m_career->repairs, m_career->repairsKept);
            m_companyMap.insert(u"currency"_s, m_careerMap.value(u"currency"_s));
        }

        Q_EMIT careerChanged();
    }

    void Controller::importDeliveries()
    {
        if (!m_career)
        {
            return;
        }

        const QList<Statistics::JobRecord> fresh = SaveGame::newDeliveries(m_career->deliveries, m_knownJobs);
        // One transaction; a few hundred rows take well under a second even on the server.
        const int written = m_sessions.writeJobs(fresh);

        if (written < 0)
        {
            Q_EMIT message(i18n("Import"), i18n("The jobs could not be written to the database."), true);
            return;
        }

        Q_EMIT message(i18n("Import"), i18np("One job imported.", "%1 jobs imported.", written), false);
        refreshSessions();
    }

    void Controller::exportSaveGame(const QUrl& file, const QString& format)
    {
        // The dump is of the save the career shows, and one export at a time.
        if (!m_career || m_exportWatcher.isRunning())
        {
            return;
        }

        m_exportTarget = file.toLocalFile();
        const auto dumpFormat =
            format == QLatin1String("json") ? SaveGame::DumpFormat::Json : SaveGame::DumpFormat::Text;
        // The file dialog has already asked whether to overwrite the target.
        m_exportWatcher.setFuture(
            QtConcurrent::run(SaveGame::exportSaveGame, m_career->profile.newestSave, m_exportTarget, dumpFormat));
    }

    bool Controller::sessionsLoading() const
    {
        return m_sessionsWatcher.isRunning();
    }

    QString Controller::sessionsError() const
    {
        return m_sessionsError;
    }

    void Controller::refreshSessions()
    {
        if (m_sessionsWatcher.isRunning())
        {
            m_sessionsStale = true;
            return;
        }

        // The store opens its own connection for every call, so it can be read from another thread.
        const Statistics::SessionStore store = m_sessions;
        // The game's names in the application's language: "de" is the game's "de_de".
        const QString language = gameLanguage();
        m_sessionsWatcher.setFuture(QtConcurrent::run(
            [store, language]
            {
                LoadedSessions loaded;

                auto sessions = store.sessions();

                if (!sessions)
                {
                    loaded.error = sessions.error();
                    return loaded;
                }

                loaded.sessions = std::move(*sessions);
                // The three come from one database; when the first could be read, the others can too, and a
                // failure among them leaves that list empty.
                loaded.jobs = store.jobs(2000, language).value_or(QList<Statistics::JobRecord>{});
                loaded.events = store.events().value_or(QList<Statistics::GameEventRecord>{});
                return loaded;
            }));
        Q_EMIT sessionsChanged();
    }

    void Controller::refreshGameNames()
    {
        if (!m_telemetryPluginInstalled)
        {
            return;
        }

        // The names come from the games' archives, which change only with an update; the fingerprint of their files
        // says when. A few seconds when they did, and not on the window's thread.
        const Statistics::SessionStore store = m_sessions;
        m_gameNamesWatcher.setFuture(QtConcurrent::run(
            [store]
            {
                bool written = false;

                for (const SaveGame::GameInstall& install : SaveGame::findGames(SaveGame::steamRoots()))
                {
                    const QString fingerprint = GameData::archivesFingerprint(install.directory);

                    if (store.gameLocaleFingerprint(SaveGame::telemetryGame(install.game)) == fingerprint)
                    {
                        continue;
                    }

                    const QList<GameData::GameName> names = GameData::readGameNames(install.directory);
                    qCDebug(WHEELSTAR) << "Read" << names.size() << "names from" << install.directory;
                    written =
                        store.writeGameLocale(SaveGame::telemetryGame(install.game), fingerprint, names) || written;
                }

                return written;
            }));
    }

    void Controller::onSessionsLoaded()
    {
        const LoadedSessions loaded = m_sessionsWatcher.result();

        m_sessionList.clear();

        for (const Statistics::SessionSummary& session : loaded.sessions)
        {
            m_sessionList.append(sessionToVariant(session, loaded.events));
        }

        m_jobList.clear();
        m_knownJobs = loaded.jobs;
        const GameNames names(loaded.jobs);

        for (const Statistics::JobRecord& job : loaded.jobs)
        {
            m_jobList.append(jobToVariant(job, names));
        }

        m_sessionsError = loaded.error;
        Q_EMIT sessionsChanged();
        // How many deliveries of the save game are new depends on the jobs just read.
        updateCareer();

        if (std::exchange(m_sessionsStale, false))
        {
            refreshSessions();
        }
    }

    bool Controller::telemetryActive() const
    {
        return m_telemetry.isActive();
    }

    QString Controller::telemetryGame() const
    {
        return gameName(m_telemetry.game());
    }

    double Controller::telemetrySpeed() const
    {
        return m_telemetry.speedKilometresPerHour();
    }

    int Controller::telemetryGear() const
    {
        return m_telemetry.gear();
    }

    Controller* Controller::create(QQmlEngine* /*qmlEngine*/, QJSEngine* engine)
    {
        // main() owns it; QML must not delete it when the engine goes.
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
        Q_UNUSED(engine)
        return s_instance;
    }

    QString Controller::profileDirectory()
    {
        return configDirectory() + "/profiles"_L1;
    }

    QVariantList Controller::devices() const
    {
        QVariantList result;

        for (const WheelDevice* device : m_deviceManager->devices())
        {
            if (device->isReady())
            {
                result.append(QVariantMap{{u"id"_s, device->id()}, {u"name"_s, device->name()}});
            }
        }

        return result;
    }

    QString Controller::currentDeviceId() const
    {
        return m_device ? m_device->id() : QString();
    }

    void Controller::selectDevice(const QString& id)
    {
        if (id == currentDeviceId())
        {
            return;
        }

        if (WheelDevice* device = m_deviceManager->device(id); device != nullptr && device->isReady())
        {
            attachDevice(device);
        }
    }

    bool Controller::hasDevice() const
    {
        return m_device != nullptr;
    }

    int Controller::maxRange() const
    {
        return m_device ? m_device->maxRange() : 900;
    }

    QVariantList Controller::modes() const
    {
        QVariantList result;

        if (m_device)
        {
            for (const auto& mode : m_device->modes())
            {
                result.append(QVariantMap{{u"id"_s, mode.id}, {u"name"_s, mode.name}});
            }
        }

        return result;
    }

    bool Controller::permissionsMissing() const
    {
        return m_device && !m_device->hasPermissions();
    }

    QStringList Controller::profiles() const
    {
        return m_profiles;
    }

    QString Controller::currentProfile() const
    {
        const DeviceState* current = currentState();
        return current != nullptr ? current->profile : QString();
    }

    bool Controller::profileDirty() const
    {
        const DeviceState* current = currentState();
        return current != nullptr && !current->profile.isEmpty() && current->settings != current->reference;
    }

    QVariantMap Controller::settings() const
    {
        const DeviceState* current = currentState();
        return current != nullptr ? current->settings.toVariantMap() : QVariantMap();
    }

    QVariantMap Controller::buttonNames() const
    {
        QVariantMap result;

        for (auto name = m_buttonNames.cbegin(); name != m_buttonNames.cend(); ++name)
        {
            result.insert(QString::number(name.key()), name.value());
        }

        return result;
    }

    QString Controller::buttonNamesPath()
    {
        return configDirectory() + "/button-names.toml"_L1;
    }

    void Controller::reloadButtonNames()
    {
        const DeviceState* const current = currentState();
        m_buttonNames = m_device ? Settings::loadButtonNames(buttonNamesPath(), m_device->name(),
                                                             current != nullptr ? current->profile : QString())
                                 : Settings::ButtonLabels();
        Q_EMIT buttonNamesChanged();
    }

    void Controller::setButtonName(int index, const QString& name)
    {
        if (!m_device)
        {
            return;
        }

        if (const QString trimmed = name.trimmed(); trimmed.isEmpty())
        {
            m_buttonNames.remove(index);
        }
        else
        {
            m_buttonNames.insert(index, trimmed);
        }

        if (const auto saved =
                Settings::saveButtonNames(buttonNamesPath(), m_device->name(), state().profile, m_buttonNames);
            !saved)
        {
            Q_EMIT message(i18n("Error saving button names"), saved.error(), true);
        }

        Q_EMIT buttonNamesChanged();
    }

    void Controller::setButtonNote(int index, const QString& note)
    {
        if (!m_device)
        {
            return;
        }

        DeviceState& current = state();

        if (const QString trimmed = note.trimmed(); trimmed.isEmpty())
        {
            current.settings.buttonNotes.remove(index);
        }
        else
        {
            current.settings.buttonNotes.insert(index, trimmed);
        }

        Q_EMIT settingsChanged();
    }

    Controller::DeviceState& Controller::state()
    {
        // Inserts on purpose: the first access for a device creates its state.
        return *m_states.tryEmplace(currentDeviceId()).iterator;
    }

    const Controller::DeviceState* Controller::currentState() const
    {
        const auto found = m_states.constFind(currentDeviceId());
        return found == m_states.cend() ? nullptr : &*found;
    }

    void Controller::notifyDeviceChanges()
    {
        QHash<QString, QString> ready;

        for (const WheelDevice* device : m_deviceManager->devices())
        {
            if (device->isReady())
            {
                ready.insert(device->id(), device->name());
            }
        }

        // A mode switch disconnects and reconnects the wheel on purpose; that is not news.
        if (!m_pendingState)
        {
            for (const auto& [id, name] : ready.asKeyValueRange())
            {
                if (!m_readyDevices.contains(id))
                {
                    KNotification::event(u"wheelConnected"_s, i18n("Wheel connected"), name, u"input-gaming"_s);
                }
            }

            for (const auto& [id, name] : m_readyDevices.asKeyValueRange())
            {
                if (!ready.contains(id))
                {
                    KNotification::event(u"wheelDisconnected"_s, i18n("Wheel disconnected"), name, u"input-gaming"_s);
                }
            }
        }

        m_readyDevices = std::move(ready);
    }

    void Controller::updateDevices()
    {
        notifyDeviceChanges();

        // The selected wheel went away - unplugged, or switching modes. Its replacement is picked up below.
        // A removed wheel is still alive here and only deleted later, so the list is what says it is gone:
        // detaching now stops a running test before its thread can reach a wheel that is being destroyed.
        if (!m_device || !m_deviceManager->devices().contains(m_device.data()))
        {
            detachDevice();
        }

        if (!m_device)
        {
            const auto devices = m_deviceManager->devices();
            if (const auto ready = std::ranges::find_if(devices, &WheelDevice::isReady); ready != devices.end())
            {
                attachDevice(*ready);
            }
        }

        Q_EMIT devicesChanged();
    }

    void Controller::detachDevice()
    {
        cancelTest();
        setGrabbed(false);

        if (m_device)
        {
            disconnect(m_device, nullptr, this, nullptr);
            m_device->close();
        }

        m_device = nullptr;
        m_steering = m_throttle = m_brake = m_clutch = 0;
        m_hatX = m_hatY = 0;
        m_buttons = 0;
        reloadButtonNames();
        Q_EMIT inputChanged();
        Q_EMIT deviceChanged();
        Q_EMIT settingsChanged();
        updateOverlay();
    }

    void Controller::attachDevice(WheelDevice* device)
    {
        detachDevice();
        m_device = device;
        m_device->open();

        // A removed device is deleted later; dropping the pointer here keeps every other path from using it.
        connect(m_device, &QObject::destroyed, this, [this] { updateDevices(); });
        connect(m_device, &WheelDevice::steeringChanged, this,
                [this](double position, qint64 timestamp)
                {
                    m_steering = position;

                    if (m_test)
                    {
                        m_test->steeringSample(position, timestamp);
                    }

                    Q_EMIT inputChanged();
                });
        connect(m_device, &WheelDevice::pedalChanged, this,
                [this](int axis, double pressed)
                {
                    switch (axis)
                    {
                        case ABS_Z:
                            m_throttle = pressed;
                            break;
                        case ABS_RZ:
                            m_brake = pressed;
                            break;
                        default:
                            m_clutch = pressed;
                            break;
                    }

                    Q_EMIT inputChanged();
                });
        connect(m_device, &WheelDevice::hatChanged, this, &Controller::onHat);
        connect(m_device, &WheelDevice::buttonChanged, this, &Controller::onButton);

        if (!m_states.contains(device->id()))
        {
            DeviceState initial;

            if (m_pendingState.has_value())
            {
                // Back after a mode switch: what was asked for before it still applies.
                initial = std::move(*m_pendingState);
                m_pendingState.reset();
            }
            else
            {
                initial.settings = WheelSettings::fromDevice(*device);

                if (!m_initialStateUsed)
                {
                    // The command line applies to the first wheel only, as upstream did.
                    m_initialStateUsed = true;

                    if (!m_options.profile.isEmpty())
                    {
                        if (const auto loaded = WheelSettings::load(profilePath(m_options.profile)))
                        {
                            initial.settings.overrideWith(*loaded);
                            initial.profile = m_options.profile;
                        }
                    }

                    initial.settings.overrideWith(m_options.settings);
                    initial.reference = initial.settings;
                }
            }

            m_states.insert(device->id(), initial);
        }

        reloadButtonNames();
        Q_EMIT deviceChanged();

        if (!device->hasPermissions() && WheelstarSettings::checkPermissions())
        {
            Q_EMIT permissionsNeeded(
                QStandardPaths::locate(QStandardPaths::GenericDataLocation, "istar-wheelstar/udev/"_L1 + RulesFile),
                RulesDirectory);
        }

        applyAll();
    }

    void Controller::applyAll()
    {
        if (!m_device)
        {
            return;
        }

        DeviceState& current = state();

        // A mode switch reconnects the wheel; the rest is applied to the device that comes back.
        if (current.settings.mode && current.settings.mode != m_device->mode())
        {
            m_pendingState = current;
            m_device->setMode(*current.settings.mode);
            return;
        }

        current.settings.applyTo(*m_device);

        if (current.settings.centerWheel.value_or(false))
        {
            centerWheel();
        }

        Q_EMIT settingsChanged();
        updateOverlay();
    }

    void Controller::centerWheel()
    {
        if (!m_device)
        {
            return;
        }

        // Full autocentre for a second pulls the wheel to the middle; then back to what is configured.
        m_device->setAutocenter(100);
        QTimer::singleShot(1s, m_device,
                           [this]
                           {
                               if (m_device)
                               {
                                   m_device->setAutocenter(state().settings.autocenter.value_or(0));
                               }
                           });
    }

    void Controller::setSetting(const QString& key, const QVariant& value)
    {
        if (!m_device)
        {
            return;
        }

        DeviceState& current = state();
        WheelSettings change;

        if (!change.setValue(key, value) || !current.settings.setValue(key, value))
        {
            qCWarning(WHEELSTAR) << "Unknown setting" << key << value;
            return;
        }

        if (key == "mode"_L1)
        {
            applyAll();
            return;
        }

        change.applyTo(*m_device);

        if (key == "center_wheel"_L1 && value.toBool())
        {
            centerWheel();
        }

        Q_EMIT settingsChanged();
        updateOverlay();
    }

    void Controller::applyMode(const QString& mode)
    {
        setSetting(u"mode"_s, mode);
    }

    void Controller::changeRangeBy(int delta)
    {
        if (m_device)
        {
            const int range = state().settings.range.value_or(m_device->maxRange());
            setSetting(u"range"_s, std::clamp(range + delta, MinimumRange, m_device->maxRange()));
        }
    }

    void Controller::refresh()
    {
        reloadProfiles();
        updateDevices();
    }

    QString Controller::profilePath(const QString& name)
    {
        return profileDirectory() + u'/' + name + ".toml"_L1;
    }

    void Controller::reloadProfiles()
    {
        QDir const directory(profileDirectory());
        m_profiles.clear();

        for (const QFileInfo& file : directory.entryInfoList({u"*.toml"_s}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        {
            m_profiles.append(file.completeBaseName());
        }

        Q_EMIT profilesChanged();
    }

    void Controller::migrateUpstreamProfiles()
    {
        const QDir directory(profileDirectory());

        if (directory.exists())
        {
            return;
        }

        // First start: bring over upstream Oversteer's profiles, which it keeps as configparser files.
        directory.mkpath(u"."_s);
        QFile::setPermissions(directory.path(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

        const QDir upstream(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                            "/oversteer/profiles"_L1);

        for (const QFileInfo& file : upstream.entryInfoList({u"*.ini"_s}, QDir::Files))
        {
            if (const auto settings = WheelSettings::load(file.filePath()))
            {
                if (const auto saved = settings->save(profilePath(file.completeBaseName())); saved)
                {
                    qCInfo(WHEELSTAR) << "Imported the upstream profile" << file.filePath();
                }
                else
                {
                    qCWarning(WHEELSTAR) << saved.error();
                }
            }
        }
    }

    void Controller::loadProfile(const QString& name)
    {
        if (!m_device)
        {
            return;
        }

        DeviceState& current = state();

        if (name.isEmpty())
        {
            current.profile.clear();
            reloadButtonNames();
            Q_EMIT settingsChanged();
            return;
        }

        const auto loaded = WheelSettings::load(profilePath(name));

        if (!loaded)
        {
            Q_EMIT message(i18n("Error opening profile"), i18n("The selected profile can't be loaded."), true);
            return;
        }

        // A profile that leaves a setting out leaves it as the wheel has it, rather than disabling the control.
        current.settings = WheelSettings::fromDevice(*m_device);
        current.settings.overrideWith(*loaded);
        current.reference = current.settings;
        current.profile = name;
        reloadButtonNames();
        applyAll();
    }

    void Controller::saveProfile()
    {
        if (!m_device || state().profile.isEmpty())
        {
            return;
        }

        DeviceState& current = state();

        if (const auto saved = current.settings.save(profilePath(current.profile)); !saved)
        {
            Q_EMIT message(i18n("Error creating profile"), saved.error(), true);
            return;
        }

        current.reference = current.settings;
        Q_EMIT settingsChanged();
    }

    bool Controller::profileExists(const QString& name) const
    {
        return QFile::exists(profilePath(name));
    }

    void Controller::createProfile(const QString& name)
    {
        const QString trimmed = name.trimmed();

        // The name becomes a file name; upstream let "../x" write outside the profile directory.
        if (!m_device || trimmed.isEmpty() || trimmed.contains(u'/') || trimmed.startsWith(u'.'))
        {
            Q_EMIT message(i18n("Error creating profile"), i18n("\"%1\" cannot be used as a profile name.", name),
                           true);
            return;
        }

        state().profile = trimmed;
        saveProfile();
        reloadProfiles();
        reloadButtonNames();
        Q_EMIT settingsChanged();
    }

    void Controller::renameProfile(const QString& from, const QString& to)
    {
        const QString trimmed = to.trimmed();

        if (trimmed.isEmpty() || trimmed.contains(u'/') || trimmed.startsWith(u'.') || profileExists(trimmed) ||
            !QFile::rename(profilePath(from), profilePath(trimmed)))
        {
            Q_EMIT message(i18n("Error renaming profile"), i18n("\"%1\" cannot be used as a profile name.", to), true);
            return;
        }

        for (DeviceState& deviceState : m_states)
        {
            if (deviceState.profile == from)
            {
                deviceState.profile = trimmed;
            }
        }

        reloadProfiles();
        Q_EMIT settingsChanged();
    }

    void Controller::deleteProfile(const QString& name)
    {
        if (!QFile::remove(profilePath(name)))
        {
            Q_EMIT message(i18n("Error deleting profile"), profilePath(name), true);
            return;
        }

        for (DeviceState& deviceState : m_states)
        {
            if (deviceState.profile == name)
            {
                deviceState.profile.clear();
            }
        }

        reloadProfiles();
        Q_EMIT settingsChanged();
    }

    void Controller::importProfile(const QUrl& file)
    {
        const QString path = file.toLocalFile();
        const QFileInfo info(path);

        if (info.suffix() != "toml"_L1 && info.suffix() != "ini"_L1)
        {
            Q_EMIT message(i18n("Error importing profile"), i18n("Invalid extension."), true);
            return;
        }

        if (profileExists(info.completeBaseName()))
        {
            Q_EMIT message(i18n("Error importing profile"), i18n("A profile with that name already exists."), true);
            return;
        }

        const auto settings = WheelSettings::load(path);
        const auto saved = settings ? settings->save(profilePath(info.completeBaseName()))
                                    : std::expected<void, QString>(std::unexpected(settings.error()));

        if (!saved)
        {
            Q_EMIT message(i18n("Error importing profile"), saved.error(), true);
            return;
        }

        reloadProfiles();
    }

    void Controller::exportProfile(const QString& name, const QUrl& file)
    {
        const QString target = file.toLocalFile();

        // The file dialog has already asked whether to overwrite it.
        QFile::remove(target);

        if (!QFile::copy(profilePath(name), target))
        {
            Q_EMIT message(i18n("Error exporting profile"), target, true);
        }
    }

    QString Controller::buttonSetupText() const
    {
        return m_setupStep ? buttonSetupLabels().at(*m_setupStep) : QString();
    }

    void Controller::startStopButtonSetup()
    {
        if (m_setupStep)
        {
            stopButtonSetup();
            return;
        }

        // Assigned into a draft: upstream overwrote the configuration at once, so stopping halfway lost it.
        m_buttonDraft = {};
        m_setupStep = 0;
        m_pressedCount = 0;
        Q_EMIT buttonSetupChanged();
    }

    void Controller::stopButtonSetup()
    {
        m_setupStep.reset();
        m_pressedCount = 0;
        Q_EMIT buttonSetupChanged();
    }

    void Controller::onHat(int axis, int value)
    {
        (axis == ABS_HAT0X ? m_hatX : m_hatY) = value;
        Q_EMIT inputChanged();

        if (value != 0)
        {
            onButton(HatButtonBase + (axis == ABS_HAT0X ? 0 : 2) + (value > 0 ? 1 : 0), true);
        }
    }

    void Controller::onButton(int index, bool pressed)
    {
        if (index < HatButtonBase)
        {
            const quint32 bit = 1U << static_cast<unsigned int>(index);

            if (pressed)
            {
                m_buttons |= bit;
                Q_EMIT inputChanged();
            }
            else
            {
                // A short press would not be seen otherwise.
                QTimer::singleShot(100ms, this,
                                   [this, bit]
                                   {
                                       m_buttons &= ~bit;
                                       Q_EMIT inputChanged();
                                   });
            }
        }

        if (pressed && m_test)
        {
            m_test->buttonPressed();
        }

        if (m_setupStep)
        {
            onSetupButton(index, pressed);
        }
        else
        {
            onWheelButton(index, pressed);
        }
    }

    void Controller::onSetupButton(int index, bool pressed)
    {
        if (*m_setupStep == 0)
        {
            // The toggle is a chord: every button held down together, finished when all are let go.
            if (index >= HatButtonBase)
            {
                return;
            }

            if (pressed)
            {
                ++m_pressedCount;
                m_buttonDraft.toggle.append(index);
                return;
            }

            if (m_pressedCount > 0)
            {
                --m_pressedCount;

                if (m_pressedCount == 0)
                {
                    m_setupStep = 1;
                    Q_EMIT buttonSetupChanged();
                }
            }

            return;
        }

        if (!pressed)
        {
            return;
        }

        m_buttonDraft.actions.at(static_cast<std::size_t>(*m_setupStep - 1)) = index;
        m_setupStep = *m_setupStep + 1;

        if (std::cmp_greater(*m_setupStep, m_buttonDraft.actions.size()))
        {
            m_buttonConfig = m_buttonDraft;
            stopButtonSetup();
            savePreferences();
        }

        Q_EMIT buttonSetupChanged();
    }

    void Controller::onWheelButton(int index, bool pressed)
    {
        if (!m_device || !state().settings.useButtons.value_or(false))
        {
            return;
        }

        if (m_grabbed && m_pressedCount == 0 && pressed)
        {
            for (const auto& [action, button] : std::views::zip(ButtonActions, m_buttonConfig.actions))
            {
                if (button == index)
                {
                    const auto& [degrees, relative] = action;
                    const int current = state().settings.range.value_or(m_device->maxRange());
                    setSetting(u"range"_s,
                               std::clamp(relative ? current + degrees : degrees, MinimumRange, m_device->maxRange()));
                }
            }
        }

        if (!m_buttonConfig.toggle.contains(index) || index >= HatButtonBase)
        {
            return;
        }

        if (!pressed)
        {
            m_pressedCount = std::max(0, m_pressedCount - 1);
            return;
        }

        ++m_pressedCount;

        if (std::cmp_equal(m_pressedCount, m_buttonConfig.toggle.size()))
        {
            setGrabbed(!m_grabbed);
        }
    }

    void Controller::setGrabbed(bool grabbed)
    {
        if (m_grabbed == grabbed)
        {
            return;
        }

        // While grabbed, games do not see the wheel, so its buttons can change the range without side effects.
        m_grabbed = grabbed && m_device && m_device->setGrabbed(true);

        if (!grabbed && m_device)
        {
            m_device->setGrabbed(false);
        }

        updateOverlay();
    }

    bool Controller::overlayShowsRange() const
    {
        const DeviceState* current = currentState();

        if (!m_device || current == nullptr || !current->settings.range)
        {
            return false;
        }

        const QString mode = current->settings.rangeOverlay.value_or(u"never"_s);
        return mode == "always"_L1 || (mode == "auto"_L1 && m_grabbed);
    }

    bool Controller::overlayShowsFfbMeter() const
    {
        const DeviceState* current = currentState();
        return m_device && current != nullptr && current->settings.ffbOverlay.value_or(false);
    }

    bool Controller::overlayVisible() const
    {
        return overlayShowsRange() || overlayShowsFfbMeter();
    }

    void Controller::updateOverlay()
    {
        if (overlayShowsFfbMeter())
        {
            m_ffbMeterTimer.start();
        }
        else
        {
            m_ffbMeterTimer.stop();
        }

        Q_EMIT overlayChanged();
    }

    void Controller::readFfbMeter()
    {
        const auto level = m_device ? m_device->attribute("peak_ffb_level") : std::nullopt;

        if (!level)
        {
            return;
        }

        // The driver keeps the peak until it is reset.
        if (*level > 0)
        {
            m_device->setAttribute("peak_ffb_level", 0);
        }

        if (const int leds = ffbMeterLeds(*level); leds != m_ffbMeterLeds)
        {
            m_ffbMeterLeds = leds;
            Q_EMIT overlayChanged();
        }
    }

    bool Controller::checkPermissions() const
    {
        return WheelstarSettings::checkPermissions();
    }

    void Controller::setCheckPermissions(bool check)
    {
        WheelstarSettings::setCheckPermissions(check);
        savePreferences();
        Q_EMIT preferencesChanged();
    }

    bool Controller::showUnusedButtons() const
    {
        return WheelstarSettings::showUnusedButtons();
    }

    void Controller::setShowUnusedButtons(bool show)
    {
        WheelstarSettings::setShowUnusedButtons(show);
        savePreferences();
        Q_EMIT preferencesChanged();
    }

    QString Controller::language() const
    {
        return WheelstarSettings::language();
    }

    void Controller::setLanguage(const QString& language)
    {
        WheelstarSettings::setLanguage(language);
        savePreferences();
        Q_EMIT preferencesChanged();
    }

    QVariantList Controller::languages() const
    {
        const std::array<std::pair<const char*, QString>, 11> entries{{
            {"", i18n("System default")},
            {"en", i18n("English")},
            {"gl", i18n("Galician")},
            {"ru", i18n("Russian")},
            {"es", i18n("Spanish")},
            {"ca", i18n("Valencian")},
            {"fi", i18n("Finnish")},
            {"tr", i18n("Turkish")},
            {"de", i18n("German")},
            {"pl", i18n("Polish")},
            {"hu", i18n("Hungarian")},
        }};

        QVariantList result;

        for (const auto& [code, name] : entries)
        {
            result.append(QVariantMap{{u"code"_s, QString::fromLatin1(code)}, {u"name"_s, name}});
        }

        return result;
    }

    void Controller::loadPreferences()
    {
        m_buttonConfig.toggle = WheelstarSettings::toggleButtons();
        const QList<int> actions = WheelstarSettings::rangeButtons();

        for (const auto& [index, button] :
             std::views::enumerate(actions | std::views::take(m_buttonConfig.actions.size())))
        {
            m_buttonConfig.actions.at(static_cast<std::size_t>(index)) = button;
        }
    }

    void Controller::savePreferences() const
    {
        WheelstarSettings::setToggleButtons(m_buttonConfig.toggle);
        WheelstarSettings::setRangeButtons(QList<int>(m_buttonConfig.actions.begin(), m_buttonConfig.actions.end()));

        if (!WheelstarSettings::self()->save())
        {
            qCWarning(WHEELSTAR) << "Cannot save the preferences";
        }
    }

    void Controller::installPermissionRules()
    {
        QString rules =
            QStandardPaths::locate(QStandardPaths::GenericDataLocation, "istar-wheelstar/udev/"_L1 + RulesFile);

        if (rules.isEmpty())
        {
            // Run from the build directory, before an install.
            rules = QStringLiteral(WHEELSTAR_SOURCE_DIR "/data/udev/") + RulesFile;
        }

        // The path goes in as an argument, never into the script, so nothing in it is ever run as root.
        auto* process = new QProcess(this);
        connect(process, &QProcess::finished, this,
                [this, process](int exitCode, QProcess::ExitStatus status)
                {
                    process->deleteLater();

                    if (status == QProcess::NormalExit && exitCode == 0)
                    {
                        Q_EMIT message(i18n("Permissions rules installed."),
                                       i18n("In some cases, a system restart might be needed."), false);
                        Q_EMIT deviceChanged();
                        return;
                    }

                    Q_EMIT message(
                        i18n("Error installing permissions rules. Please, try again and make sure you use the "
                             "right password for the administrator user."),
                        QString(), true);
                });
        process->start(u"pkexec"_s,
                       {u"/bin/sh"_s, u"-c"_s,
                        u"cp -f \"$1\" %1/ && udevadm control --reload-rules && udevadm trigger"_s.arg(RulesDirectory),
                        u"sh"_s, rules});
    }

    void Controller::applyStartupOptions(const StartupOptions& options)
    {
        if (!options.profile.isEmpty())
        {
            loadProfile(options.profile);
        }

        for (const auto& [key, value] : options.settings.toVariantMap().asKeyValueRange())
        {
            setSetting(key, value);
        }

        if (options.command.isEmpty())
        {
            return;
        }

        if (m_command != nullptr && m_command->state() != QProcess::NotRunning)
        {
            qCWarning(WHEELSTAR) << "A companion command is still running; ignoring" << options.command;
            return;
        }

        // A finished (failed) command makes room for the new one.
        if (m_command != nullptr)
        {
            m_command->deleteLater();
            m_command = nullptr;
        }

        m_options.command = options.command;
        m_options.startManually = options.startManually;
        startCommandUnlessManual();
    }

    bool Controller::commandPending() const
    {
        return !m_options.command.isEmpty() && m_command == nullptr;
    }

    void Controller::startCommandUnlessManual()
    {
        if (m_options.command.isEmpty())
        {
            return;
        }

        const bool manually = m_options.startManually.value_or(
            currentState() != nullptr && currentState()->settings.startAppManually.value_or(false));

        if (!manually && m_device)
        {
            startCommand();
        }

        Q_EMIT commandChanged();
    }

    void Controller::startCommand()
    {
        if (!commandPending())
        {
            return;
        }

        // One argument is a shell command line ("game --flag"); several are the program and its arguments, as
        // Steam's %command% expands. Upstream passed a list to the shell, which ran only its first element.
        m_command = new QProcess(this);
        m_command->setProcessChannelMode(QProcess::ForwardedChannels);

        if (m_options.command.size() == 1)
        {
            m_command->setProgram(u"/bin/sh"_s);
            m_command->setArguments({u"-c"_s, m_options.command.first()});
        }
        else
        {
            m_command->setProgram(m_options.command.first());
            m_command->setArguments(m_options.command.mid(1));
        }

        // A notification rather than a dialog: when the game fails, the window is most likely behind something.
        const auto notifyFailure = [this]
        {
            KNotification::event(u"commandFailed"_s, i18n("Command error"),
                                 i18n("The supplied command failed:\n%1", m_options.command.join(u' ')),
                                 u"dialog-error"_s);
        };

        connect(m_command, &QProcess::finished, this,
                [this, notifyFailure](int exitCode, QProcess::ExitStatus status)
                {
                    if (status != QProcess::NormalExit || exitCode != 0)
                    {
                        notifyFailure();
                        return;
                    }

                    Q_EMIT quitRequested();
                });
        connect(m_command, &QProcess::errorOccurred, this,
                [notifyFailure](QProcess::ProcessError error)
                {
                    if (error == QProcess::FailedToStart)
                    {
                        notifyFailure();
                    }
                });

        m_command->start();
        Q_EMIT commandChanged();
    }

    Controller::TestStage Controller::testStage() const
    {
        return m_testStage;
    }

    bool Controller::hasTestReport() const
    {
        return m_report.has_value();
    }

    QVariantMap Controller::testResults() const
    {
        if (!m_report)
        {
            return {};
        }

        const auto& performance = m_report->performance;
        const auto rounded = [](double value, int decimals = 0) { return QString::number(value, 'f', decimals); };

        return {
            {u"latency"_s, rounded(1000 * performance.latency)},
            {u"maxVelocity"_s, rounded(performance.maxVelocity)},
            {u"maxAcceleration"_s, rounded(performance.maxAcceleration)},
            {u"maxDeceleration"_s, rounded(performance.maxDeceleration)},
            {u"timeToMaxAcceleration"_s, rounded(1000 * performance.timeToMaxAcceleration)},
            {u"timeToMaxDeceleration"_s, rounded(1000 * performance.timeToMaxDeceleration)},
            {u"meanAcceleration"_s, rounded(performance.meanAcceleration)},
            {u"meanDeceleration"_s, rounded(performance.meanDeceleration)},
            {u"residualDeceleration"_s, rounded(performance.residualDeceleration)},
            {u"estimatedSnr"_s, rounded(performance.estimatedSnr)},
            {u"minimumLevel"_s, rounded(m_report->linear.minimumLevelPercent, 1)},
        };
    }

    void Controller::startTest()
    {
        if (!m_device)
        {
            return;
        }

        cancelTest();
        m_test = std::make_unique<FfbTest>(m_device);
        connect(m_test.get(), &FfbTest::awaitingButton, this,
                [this]
                {
                    m_testStage = TestStage::MinimumForceReady;
                    Q_EMIT testChanged();
                });
        connect(m_test.get(), &FfbTest::forceRising, this,
                [this]
                {
                    m_testStage = TestStage::MinimumForceRunning;
                    Q_EMIT testChanged();
                });
        connect(m_test.get(), &FfbTest::finished, this, &Controller::onTestFinished);

        m_testStage = TestStage::MinimumForcePrompt;
        Q_EMIT testChanged();
    }

    void Controller::runTest()
    {
        if (!m_test || m_test->isRunning())
        {
            return;
        }

        m_promptStage = m_testStage;

        switch (m_testStage)
        {
            case TestStage::MinimumForcePrompt:
                m_testStage = TestStage::MinimumForceReady;
                m_test->run(FfbTest::Stage::MinimumForce);
                break;
            case TestStage::LinearityPrompt:
                m_testStage = TestStage::Running;
                m_test->run(FfbTest::Stage::Linearity);
                break;
            case TestStage::StepPrompt:
                m_testStage = TestStage::Running;
                m_test->run(FfbTest::Stage::Step);
                break;
            default:
                return;
        }

        Q_EMIT testChanged();
    }

    void Controller::previousTest()
    {
        if (m_test && m_test->isRunning())
        {
            return;
        }

        switch (m_testStage)
        {
            case TestStage::LinearityPrompt:
                m_testStage = TestStage::MinimumForcePrompt;
                break;
            case TestStage::StepPrompt:
                m_testStage = TestStage::LinearityPrompt;
                break;
            default:
                m_test.reset();
                m_testStage = m_report ? TestStage::Results : TestStage::Idle;
                break;
        }

        Q_EMIT testChanged();
    }

    void Controller::onTestFinished(bool succeeded, const QString& error)
    {
        if (!m_test)
        {
            return;
        }

        if (!succeeded)
        {
            if (!error.isEmpty())
            {
                Q_EMIT message(i18n("Steering wheel not responding."), error, true);
            }

            m_testStage = m_promptStage;
            Q_EMIT testChanged();
            return;
        }

        switch (m_testStage)
        {
            case TestStage::MinimumForceReady:
            case TestStage::MinimumForceRunning:
                m_testStage = TestStage::LinearityPrompt;
                break;
            case TestStage::Running:
                if (m_promptStage == TestStage::LinearityPrompt)
                {
                    m_testStage = TestStage::StepPrompt;
                    break;
                }

                if (auto report = TestAnalysis::analyse(m_test->data(), m_test->wheelRange()))
                {
                    m_report = std::move(*report);
                    m_testStage = TestStage::Results;
                }
                else
                {
                    Q_EMIT message(i18n("Steering wheel not responding."), report.error(), true);
                    m_testStage = m_report ? TestStage::Results : TestStage::Idle;
                }

                m_test.reset();
                break;
            default:
                break;
        }

        Q_EMIT testChanged();
    }

    void Controller::cancelTest()
    {
        if (m_test)
        {
            m_test.reset();
            m_testStage = m_report ? TestStage::Results : TestStage::Idle;
            Q_EMIT testChanged();
        }
    }

    void Controller::importTestReport(const QUrl& file)
    {
        const auto data = TestAnalysis::importCsv(file.toLocalFile());
        const int range = m_device ? std::min(900, m_device->maxRange()) : 900;
        auto report = data ? TestAnalysis::analyse(*data, range)
                           : std::expected<TestAnalysis::Report, QString>(std::unexpected(data.error()));

        if (!report)
        {
            Q_EMIT message(i18n("CSV file to import"), report.error(), true);
            return;
        }

        m_report = std::move(*report);
        m_testStage = TestStage::Results;
        Q_EMIT testChanged();
        Q_EMIT message(i18n("Test data imported."), i18n("New test data imported from CSV file."), false);
    }

    void Controller::exportTestReport(const QUrl& file)
    {
        if (!m_report)
        {
            return;
        }

        if (const auto exported = TestAnalysis::exportCsv(file.toLocalFile(), *m_report); !exported)
        {
            Q_EMIT message(i18n("CSV file to export"), exported.error(), true);
            return;
        }

        Q_EMIT message(i18n("Test data exported."), i18n("Current test data has been exported to a CSV file."), false);
    }

    QString Controller::defaultReportName() const
    {
        return u"report-%1.csv"_s.arg(QDateTime::currentDateTime().toString(u"yyyyMMddHHmmss"_s));
    }

    QVariantMap Controller::chartData(int index) const
    {
        if (!m_report)
        {
            return {};
        }

        const auto& linear = m_report->linear;
        const auto& performance = m_report->performance;

        switch (index)
        {
            case 0:
                return chart({{.points = &linear.fixedInput, .color = "blue", .name = i18n("Input force")},
                              {.points = &linear.linearity, .color = "purple", .name = i18n("Output force")}},
                             {});
            case 1:
                return chart(
                    {{.points = &performance.input, .color = "blue", .name = i18n("Input force")},
                     {.points = &performance.position, .color = "gold", .name = i18n("Angular displacement (raw)")},
                     {.points = &performance.filteredPosition,
                      .color = "darkorange",
                      .name = i18n("Angular displacement (smoothed)")}},
                    {{.points = &performance.velocity, .color = "lightgreen", .name = i18n("Angular velocity (raw)")},
                     {.points = &performance.filteredVelocity,
                      .color = "green",
                      .name = i18n("Angular velocity (smoothed)")}});
            case 2:
                return chart({{.points = &performance.input, .color = "blue", .name = i18n("Input force")}},
                             {{.points = &performance.acceleration,
                               .color = "orange",
                               .name = i18n("Angular acceleration (raw)")},
                              {.points = &performance.filteredAcceleration,
                               .color = "red",
                               .name = i18n("Angular acceleration (smoothed)")}});
            default:
                return {};
        }
    }
} // namespace Wheelstar::Ui
