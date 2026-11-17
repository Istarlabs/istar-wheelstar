// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionstore.h"

#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Wheelstar::Statistics;
using Wheelstar::Telemetry::Game;

namespace
{
    SessionSummary sample(int startMinute)
    {
        SessionSummary summary;
        summary.game = Game::EuroTruckSimulator2;
        summary.startedAt = QDateTime(QDate(2026, 11, 12), QTime(8, startMinute), QTimeZone::UTC);
        summary.endedAt = summary.startedAt.addSecs(1800);
        summary.drivingSeconds = 1700;
        summary.movingSeconds = 1500;
        summary.distanceMetres = 30000;
        summary.maxSpeed = 25;
        summary.steeringCorrections = 42;
        summary.hardBrakes = 3;
        summary.steeringSeconds.at(10) = 700;
        summary.gearSeconds.insert(8, 1200);
        summary.gearSeconds.insert(-1, 20);
        return summary;
    }

    int pendingFiles(const QString& directory)
    {
        return static_cast<int>(QDir(directory).entryList({QStringLiteral("*.json")}, QDir::Files).size());
    }

    DatabaseConnection connection(const QString& driver, const QString& name = {})
    {
        DatabaseConnection result;
        result.driver = driver;
        result.name = name;
        return result;
    }
} // namespace

class SessionStoreTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    [[nodiscard]] DatabaseConnection database() const
    {
        return connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("test.sqlite")));
    }

    [[nodiscard]] QString pending() const
    {
        return m_directory.filePath(QStringLiteral("pending"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
    }

    void aSavedSessionIsReadBack()
    {
        SessionStore store(database(), pending());
        QVERIFY(store.save(sample(0)));

        const auto sessions = store.sessions();

        QVERIFY(sessions.has_value());
        QCOMPARE(sessions->size(), 1);
        QCOMPARE(sessions->first().game, Game::EuroTruckSimulator2);
        QCOMPARE(sessions->first().startedAt, sample(0).startedAt);
        QCOMPARE(sessions->first().distanceMetres, 30000.0);
        QCOMPARE(sessions->first().steeringCorrections, 42);
        QCOMPARE(sessions->first().steeringSeconds.at(10), 700.0);
        QCOMPARE(sessions->first().gearSeconds, sample(0).gearSeconds);
        QVERIFY(!sessions->first().gameDistanceMetres.has_value());
        QVERIFY(!sessions->first().detailed);
    }

    void theGameDistanceIsKept()
    {
        SessionStore store(
            connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("distance.sqlite"))), pending());
        SessionSummary summary = sample(0);
        summary.gameDistanceMetres = 912345.0;
        summary.detailed = true;
        summary.fuelLitres = 321.5;
        summary.cargoDamage = 0.02;
        QVERIFY(store.save(summary));

        const SessionSummary read = store.sessions()->first();
        QCOMPARE(read.gameDistanceMetres, std::optional<double>(912345.0));
        QVERIFY(read.detailed);
        QCOMPARE(read.fuelLitres, 321.5);
        QCOMPARE(read.cargoDamage, 0.02);
    }

    void jobsAndEventsAreKeptOnce()
    {
        SessionStore store(connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("jobs.sqlite"))),
                           pending());

        JobRecord job;
        job.game = Game::EuroTruckSimulator2;
        job.gameTime = 5000;
        job.sourceCompanyId = QStringLiteral("posped");
        job.destinationCompanyId = QStringLiteral("tradeaux");
        job.cargoId = QStringLiteral("gravel");
        job.revenue = 25100;
        job.finishedAt = QDateTime(QDate(2026, 11, 15), QTime(17, 0), QTimeZone::UTC);
        QVERIFY(store.saveJob(job));

        // The same delivery from the save game, which has no real time, is not a second row; an older one is.
        JobRecord fromSave = job;
        fromSave.origin = JobRecord::Origin::SaveGame;
        fromSave.finishedAt = {};
        JobRecord older = fromSave;
        older.gameTime = 4000;
        QCOMPARE(store.writeJobs({fromSave, older}), 1);

        const auto jobs = store.jobs();
        QVERIFY(jobs.has_value());
        QCOMPARE(jobs->size(), 2);
        QCOMPARE(jobs->first().revenue, 25100);
        QCOMPARE(jobs->first().finishedAt, job.finishedAt);
        QVERIFY(!jobs->last().finishedAt.isValid());

        GameEventRecord fine;
        fine.game = Game::EuroTruckSimulator2;
        fine.occurredAt = job.finishedAt;
        fine.detail = QStringLiteral("speeding");
        fine.amount = 300;
        QVERIFY(store.saveEvent(fine));
        QVERIFY(store.saveEvent(fine));
        QCOMPARE(store.events()->size(), 1);
        QCOMPARE(store.events()->first().detail, QStringLiteral("speeding"));
    }

    void aJobThatCannotBeWrittenWaitsForLater()
    {
        const QString waiting = m_directory.filePath(QStringLiteral("waiting-jobs"));
        JobRecord job;
        job.finishedAt = QDateTime::currentDateTimeUtc();
        job.gameTime = 7;

        SessionStore unreachable(connection(QStringLiteral("QNOSUCHDRIVER")), waiting);
        QVERIFY(!unreachable.saveJob(job));
        QCOMPARE(pendingFiles(waiting), 1);

        SessionStore reachable(
            connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("later-jobs.sqlite"))), waiting);
        QCOMPARE(reachable.flushPending(), 0);
        QCOMPARE(reachable.jobs()->size(), 1);
    }

    void aTableFromBeforeTheOdometerGetsTheColumn()
    {
        const QString path = m_directory.filePath(QStringLiteral("old.sqlite"));

        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old"));
            database.setDatabaseName(path);
            QVERIFY(database.open());
            // The table as version 0.1.0 created it, with one session in it.
            QVERIFY(QSqlQuery(database).exec(QStringLiteral(
                "CREATE TABLE sessions (id INTEGER PRIMARY KEY, game TEXT NOT NULL, started_at TEXT NOT NULL, "
                "ended_at TEXT NOT NULL, driving_seconds DOUBLE PRECISION NOT NULL, moving_seconds DOUBLE PRECISION "
                "NOT NULL, distance_metres DOUBLE PRECISION NOT NULL, max_speed DOUBLE PRECISION NOT NULL, "
                "steering_corrections INTEGER NOT NULL, throttle_idle_seconds DOUBLE PRECISION NOT NULL, "
                "throttle_full_seconds DOUBLE PRECISION NOT NULL, brake_seconds DOUBLE PRECISION NOT NULL, "
                "hard_brakes INTEGER NOT NULL, steering_seconds TEXT NOT NULL, gear_seconds TEXT NOT NULL, "
                "UNIQUE (game, started_at))")));
            QVERIFY(QSqlQuery(database).exec(QStringLiteral(
                "INSERT INTO sessions VALUES (1, 'ets2', '2026-11-11T08:00:00.000Z', '2026-11-11T09:00:00.000Z', "
                "3600, 3000, 50000, 25, 10, 0, 0, 0, 0, '[]', '{}')")));
            database.close();
        }

        QSqlDatabase::removeDatabase(QStringLiteral("old"));

        SessionStore store(connection(QStringLiteral("QSQLITE"), path), pending());
        SessionSummary summary = sample(0);
        summary.gameDistanceMetres = 1000.0;
        QVERIFY(store.save(summary));

        const auto sessions = store.sessions();

        QVERIFY(sessions.has_value());
        QCOMPARE(sessions->size(), 2);
        // Newest first: the new one has its distance, the old one none.
        QCOMPARE(sessions->at(0).gameDistanceMetres, std::optional<double>(1000.0));
        QVERIFY(!sessions->at(1).gameDistanceMetres.has_value());
    }

    void theDatabaseNamesTheJobs()
    {
        SessionStore store(connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("names.sqlite"))),
                           pending());
        QVERIFY(store.gameLocaleFingerprint(Game::EuroTruckSimulator2).isEmpty());

        const QList<Wheelstar::GameData::GameName> names = {
            {.kind = QStringLiteral("city"),
             .id = QStringLiteral("wroclaw"),
             .translations = {{QStringLiteral("native"), QStringLiteral("Wrocław")},
                              {QStringLiteral("de_de"), QStringLiteral("Breslau")}}},
            {.kind = QStringLiteral("city"),
             .id = QStringLiteral("berlin"),
             .translations = {{QStringLiteral("native"), QStringLiteral("Berlin")}}},
            {.kind = QStringLiteral("company"),
             .id = QStringLiteral("polarislines"),
             .translations = {{QStringLiteral("native"), QStringLiteral("Polaris Lines")}}},
            {.kind = QStringLiteral("cargo"),
             .id = QStringLiteral("sandwch_pnls"),
             .translations = {{QStringLiteral("de_de"), QStringLiteral("Sandwichpaneele")}}},
        };
        QVERIFY(store.writeGameLocale(Game::EuroTruckSimulator2, QStringLiteral("first"), names));
        QCOMPARE(store.gameLocaleFingerprint(Game::EuroTruckSimulator2), QStringLiteral("first"));

        // A job from the save game, with ids only; and one the telemetry named, in English.
        JobRecord fromSave;
        fromSave.game = Game::EuroTruckSimulator2;
        fromSave.origin = JobRecord::Origin::SaveGame;
        fromSave.gameTime = 100;
        fromSave.sourceCityId = QStringLiteral("wroclaw");
        fromSave.sourceCompanyId = QStringLiteral("polarislines");
        fromSave.destinationCityId = QStringLiteral("nowhere");
        fromSave.cargoId = QStringLiteral("sandwch_pnls");
        JobRecord named = fromSave;
        named.origin = JobRecord::Origin::Telemetry;
        named.gameTime = 200;
        named.sourceCityId = QStringLiteral("berlin");
        named.sourceCity = QStringLiteral("Berlin (telemetry)");
        named.destinationCityId = QStringLiteral("wroclaw");
        named.destinationCity = QStringLiteral("Wroclaw (telemetry)");
        QCOMPARE(store.writeJobs({fromSave, named}), 2);

        // In German ("de_DE" as QLocale names it): the game's German name first, then the telemetry's, then the
        // definition's; an id nothing knows stays without a name.
        const auto german = store.jobs(2000, QStringLiteral("de_DE"));
        QVERIFY(german.has_value());
        QCOMPARE(german->at(1).sourceCity, QStringLiteral("Breslau"));
        QCOMPARE(german->at(1).sourceCompany, QStringLiteral("Polaris Lines"));
        QCOMPARE(german->at(1).cargo, QStringLiteral("Sandwichpaneele"));
        QVERIFY(german->at(1).destinationCity.isEmpty());
        QCOMPARE(german->at(0).sourceCity, QStringLiteral("Berlin (telemetry)"));
        QCOMPARE(german->at(0).destinationCity, QStringLiteral("Breslau"));

        // Only the language: any of its regions will do.
        QCOMPARE(store.jobs(2000, QStringLiteral("de"))->at(1).sourceCity, QStringLiteral("Breslau"));

        // In a language the game has no name for: the telemetry's, else the definition's.
        const auto polish = store.jobs(2000, QStringLiteral("pl_PL"));
        QCOMPARE(polish->at(1).sourceCity, QStringLiteral("Wrocław"));
        QCOMPARE(polish->at(0).destinationCity, QStringLiteral("Wroclaw (telemetry)"));
        QVERIFY(polish->at(1).cargo.isEmpty());

        // Written again, the names are replaced, not added to.
        QVERIFY(store.writeGameLocale(Game::EuroTruckSimulator2, QStringLiteral("second"), {names.first()}));
        QCOMPARE(store.gameLocaleFingerprint(Game::EuroTruckSimulator2), QStringLiteral("second"));
        QVERIFY(store.jobs(2000, QStringLiteral("de_DE"))->at(1).sourceCompany.isEmpty());
    }

    void aJobsTableFromBeforeTheBusesGetsTheColumns()
    {
        const QString path = m_directory.filePath(QStringLiteral("old-jobs.sqlite"));

        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("old-jobs"));
            database.setDatabaseName(path);
            QVERIFY(database.open());
            // The table as #33 created it, with one truck job in it.
            QVERIFY(QSqlQuery(database).exec(QStringLiteral(
                "CREATE TABLE jobs (id INTEGER PRIMARY KEY, game TEXT NOT NULL, origin TEXT NOT NULL, delivered "
                "BOOLEAN NOT NULL, taken_at TEXT, finished_at TEXT, game_time BIGINT NOT NULL, source_city_id TEXT NOT "
                "NULL, source_company_id TEXT NOT NULL, destination_city_id TEXT NOT NULL, destination_company_id "
                "TEXT NOT NULL, cargo_id TEXT NOT NULL, source_city TEXT, source_company TEXT, destination_city TEXT, "
                "destination_company TEXT, cargo TEXT, market TEXT, truck TEXT, mass DOUBLE PRECISION, "
                "planned_kilometres DOUBLE PRECISION, driven_kilometres DOUBLE PRECISION, revenue BIGINT, experience "
                "INTEGER, cargo_damage DOUBLE PRECISION, delivery_minutes BIGINT, fines BIGINT, auto_parked BOOLEAN, "
                "auto_loaded BOOLEAN, special BOOLEAN, UNIQUE (game, game_time, source_company_id, "
                "destination_company_id, cargo_id))")));
            QVERIFY(QSqlQuery(database).exec(QStringLiteral(
                "INSERT INTO jobs (game, origin, delivered, game_time, source_city_id, source_company_id, "
                "destination_city_id, destination_company_id, cargo_id, revenue) VALUES ('ets2', 'save', 1, 100, "
                "'bremen', 'posped', 'kiel', 'tradeaux', 'gravel', 5000)")));
            database.close();
        }

        QSqlDatabase::removeDatabase(QStringLiteral("old-jobs"));

        JobRecord bus;
        bus.game = Game::EuroTruckSimulator2;
        bus.origin = JobRecord::Origin::SaveGame;
        bus.vehicle = JobRecord::Vehicle::Bus;
        bus.gameTime = 200;
        bus.sourceCityId = QStringLiteral("zurich");
        bus.destinationCityId = QStringLiteral("geneve");
        bus.passengers = 84;
        bus.stops = 2;
        bus.satisfaction = 0.75;

        SessionStore store(connection(QStringLiteral("QSQLITE"), path), pending());
        QCOMPARE(store.writeJobs({bus}), 1);

        const auto jobs = store.jobs();

        QVERIFY(jobs.has_value());
        QCOMPARE(jobs->size(), 2);
        // Latest first: the bus job, then the truck job from before the column, which reads as a truck's.
        QCOMPARE(jobs->at(0).vehicle, JobRecord::Vehicle::Bus);
        QCOMPARE(jobs->at(0).passengers, 84);
        QCOMPARE(jobs->at(0).stops, 2);
        QCOMPARE(jobs->at(0).satisfaction, 0.75);
        QCOMPARE(jobs->at(1).vehicle, JobRecord::Vehicle::Truck);
        QCOMPARE(jobs->at(1).revenue, 5000);
    }

    void theSameSessionIsNotWrittenTwice()
    {
        SessionStore store(database(), pending());
        QVERIFY(store.save(sample(0)));
        QVERIFY(store.save(sample(0)));
        QVERIFY(store.save(sample(30)));

        const auto sessions = store.sessions();

        QVERIFY(sessions.has_value());
        QCOMPARE(sessions->size(), 2);
        // Newest first.
        QCOMPARE(sessions->first().startedAt, sample(30).startedAt);
    }

    void aSessionThatCannotBeWrittenWaitsForLater()
    {
        const QString waiting = m_directory.filePath(QStringLiteral("waiting"));

        // A driver that does not exist stands in for a server that cannot be reached.
        SessionStore unreachable(connection(QStringLiteral("QNOSUCHDRIVER")), waiting);
        QVERIFY(!unreachable.save(sample(0)));
        QCOMPARE(pendingFiles(waiting), 1);
        QCOMPARE(unreachable.flushPending(), 1);

        // Once there is a database, the next save takes what waited along.
        SessionStore reachable(
            connection(QStringLiteral("QSQLITE"), m_directory.filePath(QStringLiteral("later.sqlite"))), waiting);
        QVERIFY(reachable.save(sample(30)));

        QCOMPARE(pendingFiles(waiting), 0);
        QCOMPARE(reachable.sessions()->size(), 2);
    }

    void anUnreachableDatabaseSaysWhy()
    {
        const SessionStore unreachable(connection(QStringLiteral("QNOSUCHDRIVER")), pending());
        const auto sessions = unreachable.sessions();

        QVERIFY(!sessions.has_value());
        QVERIFY(!sessions.error().isEmpty());
    }
};

QTEST_GUILESS_MAIN(SessionStoreTest)
#include "sessionstoretest.moc"
