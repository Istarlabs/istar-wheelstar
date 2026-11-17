// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/saveimport.h"

#include <QTest>

using namespace Wheelstar;

namespace
{
    /// A save with one truck delivery, one free drive (no job) and one bus job. istar-truckdata's own tests cover
    /// the reading; these cover what Wheelstar makes of it.
    constexpr const char* TextSave = R"(SiiNunit
{
economy : _nameless.2 {
 delivery_log: _nameless.3
 bus_job_log: _nameless.7
}

bus_job_log : _nameless.7 {
 entries: 1
 entries[0]: _nameless.8
}

bus_job_log_entry : _nameless.8 {
 flags: 1
 type: company_job
 assigned_bus: vehicle.volvo.9700_23
 source_bus_stop_name: city.zurich
 target_bus_stop_name: city.geneve
 stage_count: 2
 exp_delta: 438
 reward: 4515
 travelled_distance: 265
 start_time: 400620
 end_time: 400862
 total_passengers: 84
 satisfaction: &3f400000
}

delivery_log : _nameless.3 {
 entries: 2
 entries[0]: _nameless.4
 entries[1]: _nameless.6
}

delivery_log_entry : _nameless.4 {
 params: 19
 params[0]: "397719"
 params[1]: "company.volatile.polarislines.isafjordur"
 params[2]: "company.volatile.blatt_voruhu.thorshofn"
 params[3]: "cargo.sandwch_pnls"
 params[4]: "1162"
 params[5]: "35851"
 params[6]: "1002"
 params[7]: "0.016"
 params[8]: "1"
 params[9]: "0"
 params[10]: "1"
 params[11]: "1"
 params[12]: "0"
 params[13]: "38386"
 params[14]: "250"
 params[15]: "397719"
 params[16]: "vehicle.volvo.fh_2024"
 params[17]: "982"
 params[18]: "on_compn"
}

delivery_log_entry : _nameless.6 {
 params: 19
 params[0]: "225585"
 params[1]: ""
 params[2]: ""
 params[3]: ""
 params[4]: "50"
 params[5]: "0"
 params[6]: "100"
 params[7]: "0.100"
 params[8]: "0"
 params[9]: "0"
 params[10]: "0"
 params[11]: "1"
 params[12]: "0"
 params[13]: "0"
 params[14]: "0"
 params[15]: "0"
 params[16]: ""
 params[17]: "0"
 params[18]: "freerm"
}
}
)";

    class SaveImportTest : public QObject
    {
        Q_OBJECT

    private Q_SLOTS:
        void theJobsBecomeRecordsOfTheGame()
        {
            const auto document = SaveGame::readSiiFile(TextSave);
            QVERIFY2(document.has_value(), qPrintable(SaveGame::errorText(document.error_or(SaveGame::SiiError{}))));

            const QList<Statistics::JobRecord> jobs =
                SaveGame::deliveriesFrom(*document, Telemetry::Game::EuroTruckSimulator2);

            // The free drive is no job; the bus job ended last.
            QCOMPARE(jobs.size(), 2);
            const Statistics::JobRecord& truck = jobs.first();
            QCOMPARE(truck.game, Telemetry::Game::EuroTruckSimulator2);
            QCOMPARE(truck.origin, Statistics::JobRecord::Origin::SaveGame);
            QCOMPARE(truck.vehicle, Statistics::JobRecord::Vehicle::Truck);
            QVERIFY(truck.delivered);
            QCOMPARE(truck.gameTime, 397719U);
            QCOMPARE(truck.sourceCompanyId, QStringLiteral("polarislines"));
            QCOMPARE(truck.destinationCityId, QStringLiteral("thorshofn"));
            QCOMPARE(truck.cargoId, QStringLiteral("sandwch_pnls"));
            QCOMPARE(truck.revenue, 35851);
            QCOMPARE(truck.fines, 250);
            QCOMPARE(truck.truck, QStringLiteral("vehicle.volvo.fh_2024"));

            const Statistics::JobRecord& bus = jobs.last();
            QCOMPARE(bus.vehicle, Statistics::JobRecord::Vehicle::Bus);
            QCOMPARE(bus.deliveryMinutes, 242U);
            QCOMPARE(bus.passengers, 84);
            QCOMPARE(bus.satisfaction, 0.75);
        }

        void aDeliveryTheTelemetrySawIsNotNew()
        {
            const auto document = SaveGame::readSiiFile(TextSave);
            QVERIFY(document.has_value());
            const QList<Statistics::JobRecord> jobs =
                SaveGame::deliveriesFrom(*document, Telemetry::Game::EuroTruckSimulator2);

            Statistics::JobRecord seen = jobs.first();
            seen.origin = Statistics::JobRecord::Origin::Telemetry;
            seen.gameTime += 3;
            // Only the bus job is left.
            QCOMPARE(SaveGame::newDeliveries(jobs, {seen}).size(), 1);

            seen.gameTime += 600;
            QCOMPARE(SaveGame::newDeliveries(jobs, {seen}).size(), 2);
        }

        void theRepairsOfThePlayersLog()
        {
            const auto entry = [](qint64 day, qint64 revenue, qint64 maintenance, qint64 fuel, qint64 kilometres)
            {
                SaveGame::ProfitEntry result;
                result.day = day;
                result.revenue = revenue;
                result.maintenance = maintenance;
                result.fuel = fuel;
                result.kilometres = kilometres;
                return result;
            };
            SaveGame::Company company;
            // A delivery, a repair, an equal repair on the same day, fuel alone, a repair the next day.
            company.playerLog = {
                entry(275, 53230, 0, 0, 1086), entry(275, 0, 53457, 0, 0), entry(275, 0, 53457, 0, 0),
                entry(275, 0, 0, 955, 0),      entry(276, 0, 3799, 0, 0),
            };

            const QList<Statistics::RepairRecord> repairs =
                SaveGame::repairsFrom(company, Telemetry::Game::EuroTruckSimulator2);

            QCOMPARE(repairs.size(), 3);
            // Day 1 is the first day, as on the career page.
            QCOMPARE(repairs.at(0).day, 276);
            QCOMPARE(repairs.at(0).amount, 53457);
            QCOMPARE(repairs.at(0).occurrence, 0);
            QCOMPARE(repairs.at(1).occurrence, 1);
            QCOMPARE(repairs.at(2).day, 277);
            QCOMPARE(repairs.at(2).occurrence, 0);
            QCOMPARE(repairs.at(2).game, Telemetry::Game::EuroTruckSimulator2);
        }

        void theGamesKeepTheirNumbers()
        {
            QCOMPARE(SaveGame::telemetryGame(TruckData::Game::AmericanTruckSimulator),
                     Telemetry::Game::AmericanTruckSimulator);
            QCOMPARE(SaveGame::telemetryGame(TruckData::Game::Unknown), Telemetry::Game::Unknown);
        }

        void anErrorKeepsItsDetails()
        {
            const SaveGame::SiiError error{
                .kind = SaveGame::SiiError::Kind::UnknownValueType, .valueType = 0x42, .field = QStringLiteral("mode")};

            QVERIFY(SaveGame::errorText(error).contains(QStringLiteral("0x42")));
            QVERIFY(SaveGame::errorText(error).contains(QStringLiteral("mode")));
        }
    };
} // namespace

QTEST_GUILESS_MAIN(SaveImportTest)
#include "saveimporttest.moc"
