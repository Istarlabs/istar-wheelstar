// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionstore.h"
#include "core/logging.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace Wheelstar::Statistics
{
    namespace
    {
        using Telemetry::Game;

        QString gameKey(Game game)
        {
            switch (game)
            {
                case Game::EuroTruckSimulator2:
                    return u"ets2"_s;
                case Game::AmericanTruckSimulator:
                    return u"ats"_s;
                case Game::Unknown:
                    break;
            }

            return u"unknown"_s;
        }

        Game gameFromKey(const QString& key)
        {
            if (key == "ets2"_L1)
            {
                return Game::EuroTruckSimulator2;
            }

            if (key == "ats"_L1)
            {
                return Game::AmericanTruckSimulator;
            }

            return Game::Unknown;
        }

        /// A connection that exists for as long as the object does, and is gone from Qt's registry after it.
        class ScopedConnection
        {
        public:
            explicit ScopedConnection(const DatabaseConnection& settings)
                : m_name(u"wheelstar-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces))
            {
                {
                    QSqlDatabase database = QSqlDatabase::addDatabase(settings.driver, m_name);
                    database.setDatabaseName(settings.name);

                    if (settings.driver == "QPSQL"_L1)
                    {
                        database.setHostName(settings.host);
                        database.setPort(settings.port);
                        database.setUserName(settings.user);
                        // A server that does not answer must not freeze the window for the system's timeout.
                        database.setConnectOptions(u"connect_timeout=3"_s);
                    }

                    m_open = database.open();

                    if (!m_open)
                    {
                        m_error = database.lastError().text();
                        qCWarning(WHEELSTAR) << "Cannot open the statistics database:" << m_error;
                    }
                    else if (settings.driver == "QPSQL"_L1)
                    {
                        // CREATE TABLE IF NOT EXISTS says "already exists, skipping" on every connection.
                        QSqlQuery(database).exec(u"SET client_min_messages TO WARNING"_s);
                    }
                }

                if (!m_open)
                {
                    QSqlDatabase::removeDatabase(m_name);
                }
            }

            ~ScopedConnection()
            {
                if (m_open)
                {
                    QSqlDatabase::database(m_name, false).close();
                    QSqlDatabase::removeDatabase(m_name);
                }
            }

            ScopedConnection(const ScopedConnection&) = delete ("the name is in Qt's registry");
            ScopedConnection& operator=(const ScopedConnection&) = delete ("the name is in Qt's registry");
            ScopedConnection(ScopedConnection&&) = delete ("the name is in Qt's registry");
            ScopedConnection& operator=(ScopedConnection&&) = delete ("the name is in Qt's registry");

            [[nodiscard]] bool isOpen() const
            {
                return m_open;
            }

            [[nodiscard]] QSqlDatabase database() const
            {
                return QSqlDatabase::database(m_name, false);
            }

            /// Why it could not be opened.
            [[nodiscard]] const QString& error() const
            {
                return m_error;
            }

        private:
            QString m_name;
            QString m_error;
            bool m_open = false;
        };

        /// The error of a statement that failed, empty when it worked.
        QString run(QSqlQuery& query, const QString& statement)
        {
            if (!query.exec(statement))
            {
                qCWarning(WHEELSTAR) << "Statistics database:" << query.lastError().text();
                return query.lastError().text();
            }

            return {};
        }

        /// The columns after the first release, in the order they came; each is a nullable DOUBLE PRECISION. The
        /// INSERT and the SELECT below list them in the same order.
        const QStringList AddedColumns = {
            u"game_distance_metres"_s, // the odometer (#29)
            u"speeding_seconds"_s, // protocol 3 (#32), from here on
            u"speeding_metres"_s,      u"max_over_limit"_s, u"cruise_seconds"_s, u"retarder_seconds"_s,
            u"motor_brake_seconds"_s,  u"fuel_litres"_s,    u"truck_wear"_s,     u"cargo_damage"_s,
        };

        /// The columns of the jobs table that came with the buses (#41), with their types. The INSERT and the SELECT
        /// below list them in the same order.
        const QList<std::pair<QString, QString>> AddedJobColumns = {
            {u"vehicle"_s, u"TEXT"_s},
            {u"passengers"_s, u"INTEGER"_s},
            {u"stops"_s, u"INTEGER"_s},
            {u"satisfaction"_s, u"DOUBLE PRECISION"_s},
        };

        /// Adds what a table from an older version lacks; its rows keep those columns empty (NULL). SQLite knows no
        /// ADD COLUMN IF NOT EXISTS, so the table is asked first. The error, or empty when it worked.
        QString addMissingColumns(const QSqlDatabase& database, QSqlQuery& query, const QString& table,
                                  const QList<std::pair<QString, QString>>& columns)
        {
            const QSqlRecord existing = database.record(table);

            for (const auto& [column, type] : columns)
            {
                if (existing.contains(column))
                {
                    continue;
                }

                if (QString error = run(query, u"ALTER TABLE %1 ADD COLUMN %2 %3"_s.arg(table, column, type));
                    !error.isEmpty())
                {
                    return error;
                }
            }

            return {};
        }

        /// The error that kept the table from being there, empty when it is.
        QString createSchema(const QSqlDatabase& database)
        {
            const bool postgres = database.driverName() == "QPSQL"_L1;
            const QString identity = postgres ? u"BIGINT GENERATED ALWAYS AS IDENTITY"_s : u"INTEGER"_s;
            const QString timestamp = postgres ? u"TIMESTAMPTZ"_s : u"TEXT"_s;

            QSqlQuery query(database);
            QString error = run(query, uR"(
                CREATE TABLE IF NOT EXISTS sessions (
                    id %1 PRIMARY KEY,
                    game TEXT NOT NULL,
                    started_at %2 NOT NULL,
                    ended_at %2 NOT NULL,
                    driving_seconds DOUBLE PRECISION NOT NULL,
                    moving_seconds DOUBLE PRECISION NOT NULL,
                    distance_metres DOUBLE PRECISION NOT NULL,
                    max_speed DOUBLE PRECISION NOT NULL,
                    steering_corrections INTEGER NOT NULL,
                    throttle_idle_seconds DOUBLE PRECISION NOT NULL,
                    throttle_full_seconds DOUBLE PRECISION NOT NULL,
                    brake_seconds DOUBLE PRECISION NOT NULL,
                    hard_brakes INTEGER NOT NULL,
                    steering_seconds TEXT NOT NULL,
                    gear_seconds TEXT NOT NULL,
                    UNIQUE (game, started_at)
                )
            )"_s.arg(identity, timestamp));

            if (!error.isEmpty())
            {
                return error;
            }

            // The columns that came later; a session from before them has them empty, which reads as "not recorded".
            QList<std::pair<QString, QString>> sessionColumns;

            for (const QString& column : AddedColumns)
            {
                sessionColumns.append({column, u"DOUBLE PRECISION"_s});
            }

            error = addMissingColumns(database, query, u"sessions"_s, sessionColumns);

            if (!error.isEmpty())
            {
                return error;
            }

            // A job is told apart by when it ended in the game and what it was: the same delivery from the telemetry
            // and from the save game is one row.
            error = run(query, uR"(
                CREATE TABLE IF NOT EXISTS jobs (
                    id %1 PRIMARY KEY,
                    game TEXT NOT NULL,
                    origin TEXT NOT NULL,
                    delivered BOOLEAN NOT NULL,
                    taken_at %2,
                    finished_at %2,
                    game_time BIGINT NOT NULL,
                    source_city_id TEXT NOT NULL,
                    source_company_id TEXT NOT NULL,
                    destination_city_id TEXT NOT NULL,
                    destination_company_id TEXT NOT NULL,
                    cargo_id TEXT NOT NULL,
                    source_city TEXT,
                    source_company TEXT,
                    destination_city TEXT,
                    destination_company TEXT,
                    cargo TEXT,
                    market TEXT,
                    truck TEXT,
                    mass DOUBLE PRECISION,
                    planned_kilometres DOUBLE PRECISION,
                    driven_kilometres DOUBLE PRECISION,
                    revenue BIGINT,
                    experience INTEGER,
                    cargo_damage DOUBLE PRECISION,
                    delivery_minutes BIGINT,
                    fines BIGINT,
                    auto_parked BOOLEAN,
                    auto_loaded BOOLEAN,
                    special BOOLEAN,
                    UNIQUE (game, game_time, source_company_id, destination_company_id, cargo_id)
                )
            )"_s.arg(identity, timestamp));

            if (!error.isEmpty())
            {
                return error;
            }

            // A job from before the buses has no vehicle, and was a truck's.
            error = addMissingColumns(database, query, u"jobs"_s, AddedJobColumns);

            if (!error.isEmpty())
            {
                return error;
            }

            // The names of the game's cities, countries, companies and cargoes, read from its archives: one row per
            // thing, its names by language in one JSON object ({"de_de": "Breslau", …, "native": "Wrocław"}).
            error = run(query, uR"(
                CREATE TABLE IF NOT EXISTS game_locale (
                    game TEXT NOT NULL,
                    kind TEXT NOT NULL,
                    locale_id TEXT NOT NULL,
                    translations %1 NOT NULL,
                    PRIMARY KEY (game, kind, locale_id)
                )
            )"_s.arg(postgres ? u"JSONB"_s : u"TEXT"_s));

            if (!error.isEmpty())
            {
                return error;
            }

            // What the archives were when the names were read, to read them again only after an update.
            error = run(query, uR"(
                CREATE TABLE IF NOT EXISTS game_locale_sources (
                    game TEXT PRIMARY KEY,
                    fingerprint TEXT NOT NULL
                )
            )"_s);

            if (!error.isEmpty())
            {
                return error;
            }

            // The names as rows: one per thing and language. PostgreSQL has no CREATE VIEW IF NOT EXISTS, so the
            // database is asked first, as for the columns.
            if (!database.tables(QSql::Views).contains(u"game_names"_s))
            {
                error = run(query,
                            postgres ? uR"(
                    CREATE VIEW game_names AS
                    SELECT l.game, l.kind, l.locale_id, t.key AS language, t.value AS name
                    FROM game_locale l CROSS JOIN LATERAL jsonb_each_text(l.translations) AS t
                )"_s
                                     : uR"(
                    CREATE VIEW game_names AS
                    SELECT l.game, l.kind, l.locale_id, t.key AS language, t.value AS name
                    FROM game_locale l, json_each(l.translations) AS t
                )"_s);

                if (!error.isEmpty())
                {
                    return error;
                }
            }

            error = run(query, uR"(
                CREATE TABLE IF NOT EXISTS game_events (
                    id %1 PRIMARY KEY,
                    game TEXT NOT NULL,
                    kind TEXT NOT NULL,
                    occurred_at %2 NOT NULL,
                    game_time BIGINT NOT NULL,
                    detail TEXT,
                    from_place TEXT,
                    to_place TEXT,
                    amount BIGINT NOT NULL,
                    UNIQUE (game, kind, occurred_at)
                )
            )"_s.arg(identity, timestamp));

            if (!error.isEmpty())
            {
                return error;
            }

            // The repairs of the save game's own profit log, which forgets them after a few in-game days. A bill is
            // known by its day and amount; `occurrence` tells equal bills of one day apart.
            return run(query, uR"(
                CREATE TABLE IF NOT EXISTS repairs (
                    id %1 PRIMARY KEY,
                    game TEXT NOT NULL,
                    game_day BIGINT NOT NULL,
                    amount BIGINT NOT NULL,
                    occurrence INTEGER NOT NULL,
                    UNIQUE (game, game_day, amount, occurrence)
                )
            )"_s.arg(identity));
        }

        // The positions of the columns in the SELECTs below, in the order they list them. "GameKey" because
        // "Game" is the type here.
        namespace SessionColumn
        {
            constexpr int GameKey = 0;
            constexpr int StartedAt = 1;
            constexpr int EndedAt = 2;
            constexpr int DrivingSeconds = 3;
            constexpr int MovingSeconds = 4;
            constexpr int DistanceMetres = 5;
            constexpr int MaxSpeed = 6;
            constexpr int SteeringCorrections = 7;
            constexpr int ThrottleIdleSeconds = 8;
            constexpr int ThrottleFullSeconds = 9;
            constexpr int BrakeSeconds = 10;
            constexpr int HardBrakes = 11;
            constexpr int SteeringSeconds = 12;
            constexpr int GearSeconds = 13;
            // From here on AddedColumns, in its order.
            constexpr int GameDistanceMetres = 14;
            constexpr int SpeedingSeconds = 15;
            constexpr int SpeedingMetres = 16;
            constexpr int MaxOverLimit = 17;
            constexpr int CruiseSeconds = 18;
            constexpr int RetarderSeconds = 19;
            constexpr int MotorBrakeSeconds = 20;
            constexpr int FuelLitres = 21;
            constexpr int TruckWear = 22;
            constexpr int CargoDamage = 23;
        } // namespace SessionColumn

        namespace JobColumn
        {
            constexpr int GameKey = 0;
            constexpr int Origin = 1;
            constexpr int Delivered = 2;
            constexpr int TakenAt = 3;
            constexpr int FinishedAt = 4;
            constexpr int GameTime = 5;
            constexpr int SourceCityId = 6;
            constexpr int SourceCompanyId = 7;
            constexpr int DestinationCityId = 8;
            constexpr int DestinationCompanyId = 9;
            constexpr int CargoId = 10;
            constexpr int SourceCity = 11;
            constexpr int SourceCompany = 12;
            constexpr int DestinationCity = 13;
            constexpr int DestinationCompany = 14;
            constexpr int Cargo = 15;
            constexpr int Market = 16;
            constexpr int Truck = 17;
            constexpr int Mass = 18;
            constexpr int PlannedKilometres = 19;
            constexpr int DrivenKilometres = 20;
            constexpr int Revenue = 21;
            constexpr int Experience = 22;
            constexpr int CargoDamage = 23;
            constexpr int DeliveryMinutes = 24;
            constexpr int Fines = 25;
            constexpr int AutoParked = 26;
            constexpr int AutoLoaded = 27;
            constexpr int Special = 28;
            constexpr int Vehicle = 29;
            constexpr int Passengers = 30;
            constexpr int Stops = 31;
            constexpr int Satisfaction = 32;
        } // namespace JobColumn

        namespace EventColumn
        {
            constexpr int GameKey = 0;
            constexpr int Kind = 1;
            constexpr int OccurredAt = 2;
            constexpr int GameTime = 3;
            constexpr int Detail = 4;
            constexpr int From = 5;
            constexpr int To = 6;
            constexpr int Amount = 7;
        } // namespace EventColumn

        /// A name of a job's city, company or cargo, worked out by the database: from game_names in the wanted
        /// language (exactly, else any of its regions), else the one the telemetry recorded, else the game's own
        /// from its definition. NULL when none is known; the application then makes one of the id.
        QString localizedName(const QString& kind, const QString& idColumn, const QString& nameColumn)
        {
            return uR"(COALESCE(
                (SELECT n.name FROM game_names n, wanted w
                 WHERE n.game = j.game AND n.kind = '%1' AND n.locale_id = j.%2
                   AND (n.language = w.language OR substr(n.language, 1, length(w.prefix) + 1) = w.prefix || '_')
                 ORDER BY CASE WHEN n.language = w.language THEN 0 ELSE 1 END LIMIT 1),
                NULLIF(j.%3, ''),
                (SELECT n.name FROM game_names n
                 WHERE n.game = j.game AND n.kind = '%1' AND n.locale_id = j.%2 AND n.language = 'native'))
            )"_s.arg(kind, idColumn, nameColumn);
        }

        QString json(const QJsonValue& value)
        {
            if (value.isArray())
            {
                return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
            }

            return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        }
    } // namespace

    SessionStore::SessionStore(DatabaseConnection connection, QString pendingDirectory)
        : m_connection(std::move(connection)), m_pendingDirectory(std::move(pendingDirectory))
    {
    }

    bool SessionStore::write(const SessionSummary& summary) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return false;
        }

        const QJsonObject object = summary.toJson();

        QSqlQuery query(connection.database());
        QStringList placeholders;

        for (const QString& column : AddedColumns)
        {
            placeholders.append(u':' + column);
        }

        query.prepare(uR"(
            INSERT INTO sessions (game, started_at, ended_at, driving_seconds, moving_seconds, distance_metres,
                                  max_speed, steering_corrections, throttle_idle_seconds, throttle_full_seconds,
                                  brake_seconds, hard_brakes, steering_seconds, gear_seconds, %1)
            VALUES (:game, :started, :ended, :driving, :moving, :distance, :maxSpeed, :corrections, :idle, :full,
                    :brake, :hardBrakes, :steering, :gears, %2)
            ON CONFLICT (game, started_at) DO NOTHING
        )"_s.arg(AddedColumns.join(u", "_s), placeholders.join(u", "_s)));
        query.bindValue(u":game"_s, gameKey(summary.game));
        query.bindValue(u":started"_s, summary.startedAt.toUTC());
        query.bindValue(u":ended"_s, summary.endedAt.toUTC());
        query.bindValue(u":driving"_s, summary.drivingSeconds);
        query.bindValue(u":moving"_s, summary.movingSeconds);
        query.bindValue(u":distance"_s, summary.distanceMetres);
        query.bindValue(u":maxSpeed"_s, summary.maxSpeed);
        query.bindValue(u":corrections"_s, summary.steeringCorrections);
        query.bindValue(u":idle"_s, summary.throttleIdleSeconds);
        query.bindValue(u":full"_s, summary.throttleFullSeconds);
        query.bindValue(u":brake"_s, summary.brakeSeconds);
        query.bindValue(u":hardBrakes"_s, summary.hardBrakes);
        query.bindValue(u":steering"_s, json(object.value(u"steeringSeconds"_s)));
        query.bindValue(u":gears"_s, json(object.value(u"gearSeconds"_s)));
        // A null double is SQL NULL: unknown, not 0.
        const QVariant unknown(QMetaType::fromType<double>());
        const auto detail = [&](double value) { return summary.detailed ? QVariant(value) : unknown; };
        const std::array<QVariant, 10> added = {
            summary.gameDistanceMetres ? QVariant(*summary.gameDistanceMetres) : unknown,
            detail(summary.speedingSeconds),
            detail(summary.speedingMetres),
            detail(summary.maxOverLimit),
            detail(summary.cruiseSeconds),
            detail(summary.retarderSeconds),
            detail(summary.motorBrakeSeconds),
            detail(summary.fuelLitres),
            detail(summary.truckWear),
            detail(summary.cargoDamage),
        };
        static_assert(added.size() == 10, "one value per added column");

        for (qsizetype i = 0; i < AddedColumns.size(); ++i)
        {
            query.bindValue(u':' + AddedColumns.at(i), added.at(static_cast<std::size_t>(i)));
        }

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot write the session:" << query.lastError().text();
            return false;
        }

        return true;
    }

    void SessionStore::keepForLater(const QJsonObject& object, const QDateTime& when, const QString& kind) const
    {
        // The time and the kind make the name unique enough; the files are only ever read all together.
        const QString stamp = when.toUTC().toString(u"yyyyMMddTHHmmsszzz"_s);
        QDir().mkpath(m_pendingDirectory);

        if (QSaveFile file(m_pendingDirectory + u'/' + stamp + u'-' + kind + ".json"_L1);
            !file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson()) < 0 || !file.commit())
        {
            qCWarning(WHEELSTAR) << "Cannot keep the record for later either:" << file.errorString();
        }
    }

    bool SessionStore::save(const SessionSummary& summary)
    {
        if (write(summary))
        {
            flushPending();
            return true;
        }

        keepForLater(summary.toJson(), summary.startedAt, gameKey(summary.game));
        return false;
    }

    bool SessionStore::saveJob(const JobRecord& job)
    {
        if (writeJobs({job}) >= 0)
        {
            flushPending();
            return true;
        }

        keepForLater(job.toJson(), job.finishedAt, u"job"_s);
        return false;
    }

    bool SessionStore::saveEvent(const GameEventRecord& event)
    {
        if (writeEvent(event))
        {
            flushPending();
            return true;
        }

        keepForLater(event.toJson(), event.occurredAt, u"event-"_s + event.kindKey());
        return false;
    }

    int SessionStore::flushPending()
    {
        const QDir directory(m_pendingDirectory);
        const QStringList files = directory.entryList({u"*.json"_s}, QDir::Files, QDir::Name);
        int waiting = 0;

        for (const QString& name : files)
        {
            QFile file(directory.filePath(name));
            QJsonObject object;

            if (file.open(QIODevice::ReadOnly))
            {
                object = QJsonDocument::fromJson(file.readAll()).object();
                file.close();
            }

            // Sessions came first and carry no "record" key; jobs and events say what they are.
            std::optional<bool> written;

            if (const auto job = JobRecord::fromJson(object))
            {
                written = writeJobs({*job}) >= 0;
            }
            else if (const auto event = GameEventRecord::fromJson(object))
            {
                written = writeEvent(*event);
            }
            else if (const auto summary = SessionSummary::fromJson(object))
            {
                written = write(*summary);
            }

            if (!written)
            {
                qCWarning(WHEELSTAR) << "Not a record:" << file.fileName();
                continue;
            }

            if (*written)
            {
                file.remove();
            }
            else
            {
                // The server is still out of reach; the rest would fail the same way.
                waiting = static_cast<int>(files.size() - files.indexOf(name));
                break;
            }
        }

        return waiting;
    }

    int SessionStore::writeJobs(const QList<JobRecord>& jobs) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return -1;
        }

        QSqlDatabase database = connection.database();
        // One transaction, so a save game's hundreds of jobs are one commit and not hundreds.
        database.transaction();

        QSqlQuery query(database);
        query.prepare(uR"(
            INSERT INTO jobs (game, origin, delivered, taken_at, finished_at, game_time, source_city_id,
                              source_company_id, destination_city_id, destination_company_id, cargo_id, source_city,
                              source_company, destination_city, destination_company, cargo, market, truck, mass,
                              planned_kilometres, driven_kilometres, revenue, experience, cargo_damage,
                              delivery_minutes, fines, auto_parked, auto_loaded, special, vehicle, passengers, stops,
                              satisfaction)
            VALUES (:game, :origin, :delivered, :taken, :finished, :gameTime, :sourceCityId, :sourceCompanyId,
                    :destinationCityId, :destinationCompanyId, :cargoId, :sourceCity, :sourceCompany,
                    :destinationCity, :destinationCompany, :cargo, :market, :truck, :mass, :planned, :driven,
                    :revenue, :experience, :cargoDamage, :deliveryMinutes, :fines, :autoParked, :autoLoaded, :special,
                    :vehicle, :passengers, :stops, :satisfaction)
            ON CONFLICT (game, game_time, source_company_id, destination_company_id, cargo_id) DO NOTHING
        )"_s);

        int written = 0;
        // A time that is not known is NULL, not the epoch.
        const auto time = [](const QDateTime& value)
        { return value.isValid() ? QVariant(value.toUTC()) : QVariant(QMetaType::fromType<QDateTime>()); };
        // A null QString is bound as NULL; an id the program did not learn (a job whose start it missed) is empty,
        // so the unique key still compares.
        const auto id = [](const QString& value) { return value.isNull() ? u""_s : value; };

        for (const JobRecord& job : jobs)
        {
            query.bindValue(u":game"_s, gameKey(job.game));
            query.bindValue(u":origin"_s, job.origin == JobRecord::Origin::SaveGame ? u"save"_s : u"telemetry"_s);
            query.bindValue(u":delivered"_s, job.delivered);
            query.bindValue(u":taken"_s, time(job.takenAt));
            query.bindValue(u":finished"_s, time(job.finishedAt));
            query.bindValue(u":gameTime"_s, static_cast<qint64>(job.gameTime));
            query.bindValue(u":sourceCityId"_s, id(job.sourceCityId));
            query.bindValue(u":sourceCompanyId"_s, id(job.sourceCompanyId));
            query.bindValue(u":destinationCityId"_s, id(job.destinationCityId));
            query.bindValue(u":destinationCompanyId"_s, id(job.destinationCompanyId));
            query.bindValue(u":cargoId"_s, id(job.cargoId));
            query.bindValue(u":sourceCity"_s, job.sourceCity);
            query.bindValue(u":sourceCompany"_s, job.sourceCompany);
            query.bindValue(u":destinationCity"_s, job.destinationCity);
            query.bindValue(u":destinationCompany"_s, job.destinationCompany);
            query.bindValue(u":cargo"_s, job.cargo);
            query.bindValue(u":market"_s, job.market);
            query.bindValue(u":truck"_s, job.truck);
            query.bindValue(u":mass"_s, job.mass);
            query.bindValue(u":planned"_s, job.plannedKilometres);
            query.bindValue(u":driven"_s, job.drivenKilometres);
            query.bindValue(u":revenue"_s, job.revenue);
            query.bindValue(u":experience"_s, job.experience);
            query.bindValue(u":cargoDamage"_s, job.cargoDamage);
            query.bindValue(u":deliveryMinutes"_s, static_cast<qint64>(job.deliveryMinutes));
            query.bindValue(u":fines"_s, job.fines);
            query.bindValue(u":autoParked"_s, job.autoParked);
            query.bindValue(u":autoLoaded"_s, job.autoLoaded);
            query.bindValue(u":special"_s, job.special);
            query.bindValue(u":vehicle"_s, job.vehicleKey());
            query.bindValue(u":passengers"_s, job.passengers);
            query.bindValue(u":stops"_s, job.stops);
            query.bindValue(u":satisfaction"_s, job.satisfaction);

            if (!query.exec())
            {
                qCWarning(WHEELSTAR) << "Cannot write the job:" << query.lastError().text();
                database.rollback();
                return -1;
            }

            written += query.numRowsAffected() > 0 ? 1 : 0;
        }

        database.commit();
        return written;
    }

    int SessionStore::writeRepairs(const QList<RepairRecord>& repairs) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return -1;
        }

        QSqlDatabase database = connection.database();
        database.transaction();

        QSqlQuery query(database);
        query.prepare(uR"(
            INSERT INTO repairs (game, game_day, amount, occurrence) VALUES (:game, :day, :amount, :occurrence)
            ON CONFLICT (game, game_day, amount, occurrence) DO NOTHING
        )"_s);
        int written = 0;

        for (const RepairRecord& repair : repairs)
        {
            query.bindValue(u":game"_s, gameKey(repair.game));
            query.bindValue(u":day"_s, repair.day);
            query.bindValue(u":amount"_s, repair.amount);
            query.bindValue(u":occurrence"_s, repair.occurrence);

            if (!query.exec())
            {
                qCWarning(WHEELSTAR) << "Cannot write the repair:" << query.lastError().text();
                database.rollback();
                return -1;
            }

            written += query.numRowsAffected() > 0 ? 1 : 0;
        }

        database.commit();
        return written;
    }

    std::expected<QList<RepairRecord>, QString> SessionStore::repairs(Telemetry::Game game) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen())
        {
            return std::unexpected(connection.error());
        }

        if (const QString error = createSchema(connection.database()); !error.isEmpty())
        {
            return std::unexpected(error);
        }

        QSqlQuery query(connection.database());
        query.prepare(uR"(
            SELECT game_day, amount, occurrence FROM repairs WHERE game = :game
            ORDER BY game_day DESC, occurrence DESC, amount DESC
        )"_s);
        query.bindValue(u":game"_s, gameKey(game));

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot read the repairs:" << query.lastError().text();
            return std::unexpected(query.lastError().text());
        }

        QList<RepairRecord> result;

        while (query.next())
        {
            result.append({.game = game,
                           .day = query.value(0).toLongLong(),
                           .amount = query.value(1).toLongLong(),
                           .occurrence = query.value(2).toInt()});
        }

        return result;
    }

    bool SessionStore::writeEvent(const GameEventRecord& event) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return false;
        }

        QSqlQuery query(connection.database());
        query.prepare(uR"(
            INSERT INTO game_events (game, kind, occurred_at, game_time, detail, from_place, to_place, amount)
            VALUES (:game, :kind, :occurred, :gameTime, :detail, :from, :to, :amount)
            ON CONFLICT (game, kind, occurred_at) DO NOTHING
        )"_s);
        query.bindValue(u":game"_s, gameKey(event.game));
        query.bindValue(u":kind"_s, event.kindKey());
        query.bindValue(u":occurred"_s, event.occurredAt.toUTC());
        query.bindValue(u":gameTime"_s, static_cast<qint64>(event.gameTime));
        query.bindValue(u":detail"_s, event.detail);
        query.bindValue(u":from"_s, event.from);
        query.bindValue(u":to"_s, event.to);
        query.bindValue(u":amount"_s, event.amount);

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot write the event:" << query.lastError().text();
            return false;
        }

        return true;
    }

    QString SessionStore::gameLocaleFingerprint(Telemetry::Game game) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return {};
        }

        QSqlQuery query(connection.database());
        query.prepare(u"SELECT fingerprint FROM game_locale_sources WHERE game = :game"_s);
        query.bindValue(u":game"_s, gameKey(game));

        return query.exec() && query.next() ? query.value(0).toString() : QString();
    }

    std::expected<QHash<QString, QHash<QString, QString>>, QString>
    SessionStore::gameNames(Telemetry::Game game, const QString& language) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen())
        {
            return std::unexpected(connection.error());
        }

        if (const QString error = createSchema(connection.database()); !error.isEmpty())
        {
            return std::unexpected(error);
        }

        // The same choice as the jobs' names make: the language exactly, else a region of it, else the definition's.
        QSqlQuery query(connection.database());
        query.prepare(uR"(
            WITH wanted(language, prefix) AS (SELECT CAST(:language AS TEXT), CAST(:prefix AS TEXT))
            SELECT n.kind, n.locale_id, n.name,
                   CASE WHEN n.language = w.language THEN 0 WHEN n.language = 'native' THEN 2 ELSE 1 END
            FROM game_names n, wanted w
            WHERE n.game = :game
              AND (n.language = w.language OR n.language = 'native'
                   OR substr(n.language, 1, length(w.prefix) + 1) = w.prefix || '_')
        )"_s);
        query.bindValue(u":language"_s, language.toLower());
        query.bindValue(u":prefix"_s, language.section(u'_', 0, 0).toLower());
        query.bindValue(u":game"_s, gameKey(game));

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot read the game's names:" << query.lastError().text();
            return std::unexpected(query.lastError().text());
        }

        QHash<QString, QHash<QString, QString>> names;
        // The rank each name was chosen at, to let a better one replace it.
        QHash<std::pair<QString, QString>, int> ranks;

        while (query.next())
        {
            const QString kind = query.value(0).toString();
            const QString id = query.value(1).toString();
            const int rank = query.value(3).toInt();
            const auto key = std::pair(kind, id);

            if (const auto found = ranks.constFind(key); found == ranks.cend() || rank < *found)
            {
                ranks.insert(key, rank);
                // The kind's table is made by its first name.
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                names[kind].insert(id, query.value(2).toString());
            }
        }

        return names;
    }

    bool SessionStore::writeGameLocale(Telemetry::Game game, const QString& fingerprint,
                                       const QList<GameData::GameName>& names) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen() || !createSchema(connection.database()).isEmpty())
        {
            return false;
        }

        QSqlDatabase database = connection.database();
        const bool postgres = database.driverName() == "QPSQL"_L1;
        // One transaction: the names of a game are replaced all at once, or not at all.
        database.transaction();
        QSqlQuery query(database);

        const auto fail = [&]
        {
            qCWarning(WHEELSTAR) << "Cannot write the game's names:" << query.lastError().text();
            database.rollback();
            return false;
        };

        query.prepare(u"DELETE FROM game_locale WHERE game = :game"_s);
        query.bindValue(u":game"_s, gameKey(game));

        if (!query.exec())
        {
            return fail();
        }

        query.prepare(
            u"INSERT INTO game_locale (game, kind, locale_id, translations) VALUES (:game, :kind, :id, %1)"_s.arg(
                postgres ? u"CAST(:translations AS JSONB)"_s : u":translations"_s));

        for (const GameData::GameName& name : names)
        {
            query.bindValue(u":game"_s, gameKey(game));
            query.bindValue(u":kind"_s, name.kind);
            query.bindValue(u":id"_s, name.id);
            query.bindValue(u":translations"_s,
                            QString::fromUtf8(QJsonDocument(name.translations).toJson(QJsonDocument::Compact)));

            if (!query.exec())
            {
                return fail();
            }
        }

        query.prepare(uR"(
            INSERT INTO game_locale_sources (game, fingerprint) VALUES (:game, :fingerprint)
            ON CONFLICT (game) DO UPDATE SET fingerprint = excluded.fingerprint
        )"_s);
        query.bindValue(u":game"_s, gameKey(game));
        query.bindValue(u":fingerprint"_s, fingerprint);

        if (!query.exec())
        {
            return fail();
        }

        return database.commit();
    }

    std::expected<QList<JobRecord>, QString> SessionStore::jobs(int limit, const QString& language) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen())
        {
            return std::unexpected(connection.error());
        }

        if (const QString error = createSchema(connection.database()); !error.isEmpty())
        {
            return std::unexpected(error);
        }

        QSqlQuery query(connection.database());
        // The game's clock orders them: a job from the save game has no real time, and the game's minutes only run
        // forward within a profile. The names come from game_locale where it knows them.
        query.prepare(uR"(
            WITH wanted(language, prefix) AS (SELECT CAST(:language AS TEXT), CAST(:prefix AS TEXT))
            SELECT j.game, j.origin, j.delivered, j.taken_at, j.finished_at, j.game_time, j.source_city_id,
                   j.source_company_id, j.destination_city_id, j.destination_company_id, j.cargo_id, %1, %2, %3, %4,
                   %5, j.market, j.truck, j.mass, j.planned_kilometres, j.driven_kilometres, j.revenue, j.experience,
                   j.cargo_damage, j.delivery_minutes, j.fines, j.auto_parked, j.auto_loaded, j.special, j.vehicle,
                   j.passengers, j.stops, j.satisfaction
            FROM jobs j ORDER BY j.game_time DESC LIMIT :limit
        )"_s.arg(localizedName(u"city"_s, u"source_city_id"_s, u"source_city"_s),
                 localizedName(u"company"_s, u"source_company_id"_s, u"source_company"_s),
                 localizedName(u"city"_s, u"destination_city_id"_s, u"destination_city"_s),
                 localizedName(u"company"_s, u"destination_company_id"_s, u"destination_company"_s),
                 localizedName(u"cargo"_s, u"cargo_id"_s, u"cargo"_s)));
        query.bindValue(u":language"_s, language.toLower());
        query.bindValue(u":prefix"_s, language.section(u'_', 0, 0).toLower());
        query.bindValue(u":limit"_s, limit);

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot read the jobs:" << query.lastError().text();
            return std::unexpected(query.lastError().text());
        }

        QList<JobRecord> result;

        while (query.next())
        {
            using namespace JobColumn;
            const auto time = [&query](int column)
            { return query.isNull(column) ? QDateTime() : query.value(column).toDateTime().toUTC(); };

            JobRecord job;
            job.game = gameFromKey(query.value(GameKey).toString());
            job.origin = query.value(Origin).toString() == "save"_L1 ? JobRecord::Origin::SaveGame
                                                                     : JobRecord::Origin::Telemetry;
            job.delivered = query.value(Delivered).toBool();
            job.takenAt = time(TakenAt);
            job.finishedAt = time(FinishedAt);
            job.gameTime = static_cast<quint32>(query.value(GameTime).toLongLong());
            job.sourceCityId = query.value(SourceCityId).toString();
            job.sourceCompanyId = query.value(SourceCompanyId).toString();
            job.destinationCityId = query.value(DestinationCityId).toString();
            job.destinationCompanyId = query.value(DestinationCompanyId).toString();
            job.cargoId = query.value(CargoId).toString();
            job.sourceCity = query.value(SourceCity).toString();
            job.sourceCompany = query.value(SourceCompany).toString();
            job.destinationCity = query.value(DestinationCity).toString();
            job.destinationCompany = query.value(DestinationCompany).toString();
            job.cargo = query.value(Cargo).toString();
            job.market = query.value(Market).toString();
            job.truck = query.value(Truck).toString();
            job.mass = query.value(Mass).toDouble();
            job.plannedKilometres = query.value(PlannedKilometres).toDouble();
            job.drivenKilometres = query.value(DrivenKilometres).toDouble();
            job.revenue = query.value(Revenue).toLongLong();
            job.experience = query.value(Experience).toInt();
            job.cargoDamage = query.value(CargoDamage).toDouble();
            job.deliveryMinutes = static_cast<quint32>(query.value(DeliveryMinutes).toLongLong());
            job.fines = query.value(Fines).toLongLong();
            job.autoParked = query.value(AutoParked).toBool();
            job.autoLoaded = query.value(AutoLoaded).toBool();
            job.special = query.value(Special).toBool();
            job.vehicle = JobRecord::vehicleFromKey(query.value(Vehicle).toString());
            job.passengers = query.value(Passengers).toInt();
            job.stops = query.value(Stops).toInt();
            job.satisfaction = query.value(Satisfaction).toDouble();
            result.append(job);
        }

        return result;
    }

    std::expected<QList<GameEventRecord>, QString> SessionStore::events(int limit) const
    {
        const ScopedConnection connection(m_connection);

        if (!connection.isOpen())
        {
            return std::unexpected(connection.error());
        }

        if (const QString error = createSchema(connection.database()); !error.isEmpty())
        {
            return std::unexpected(error);
        }

        QSqlQuery query(connection.database());
        query.prepare(uR"(
            SELECT game, kind, occurred_at, game_time, detail, from_place, to_place, amount
            FROM game_events ORDER BY occurred_at DESC LIMIT :limit
        )"_s);
        query.bindValue(u":limit"_s, limit);

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot read the events:" << query.lastError().text();
            return std::unexpected(query.lastError().text());
        }

        QList<GameEventRecord> result;

        while (query.next())
        {
            using namespace EventColumn;
            const auto kind = GameEventRecord::kindFromKey(query.value(Kind).toString());

            if (!kind)
            {
                continue;
            }

            GameEventRecord event;
            event.game = gameFromKey(query.value(GameKey).toString());
            event.kind = *kind;
            event.occurredAt = query.value(OccurredAt).toDateTime().toUTC();
            event.gameTime = static_cast<quint32>(query.value(GameTime).toLongLong());
            event.detail = query.value(Detail).toString();
            event.from = query.value(From).toString();
            event.to = query.value(To).toString();
            event.amount = query.value(Amount).toLongLong();
            result.append(event);
        }

        return result;
    }

    std::expected<QList<SessionSummary>, QString> SessionStore::sessions(int limit) const
    {
        const ScopedConnection connection(m_connection);
        QList<SessionSummary> result;

        if (!connection.isOpen())
        {
            return std::unexpected(connection.error());
        }

        if (const QString error = createSchema(connection.database()); !error.isEmpty())
        {
            return std::unexpected(error);
        }

        QSqlQuery query(connection.database());
        query.prepare(uR"(
            SELECT game, started_at, ended_at, driving_seconds, moving_seconds, distance_metres, max_speed,
                   steering_corrections, throttle_idle_seconds, throttle_full_seconds, brake_seconds, hard_brakes,
                   steering_seconds, gear_seconds, %1
            FROM sessions ORDER BY started_at DESC LIMIT :limit
        )"_s.arg(AddedColumns.join(u", "_s)));
        query.bindValue(u":limit"_s, limit);

        if (!query.exec())
        {
            qCWarning(WHEELSTAR) << "Cannot read the sessions:" << query.lastError().text();
            return std::unexpected(query.lastError().text());
        }

        while (query.next())
        {
            using namespace SessionColumn;

            SessionSummary summary;
            summary.game = gameFromKey(query.value(GameKey).toString());
            summary.startedAt = query.value(StartedAt).toDateTime().toUTC();
            summary.endedAt = query.value(EndedAt).toDateTime().toUTC();
            summary.drivingSeconds = query.value(DrivingSeconds).toDouble();
            summary.movingSeconds = query.value(MovingSeconds).toDouble();
            summary.distanceMetres = query.value(DistanceMetres).toDouble();
            summary.maxSpeed = query.value(MaxSpeed).toDouble();
            summary.steeringCorrections = query.value(SteeringCorrections).toInt();
            summary.throttleIdleSeconds = query.value(ThrottleIdleSeconds).toDouble();
            summary.throttleFullSeconds = query.value(ThrottleFullSeconds).toDouble();
            summary.brakeSeconds = query.value(BrakeSeconds).toDouble();
            summary.hardBrakes = query.value(HardBrakes).toInt();

            if (!query.isNull(GameDistanceMetres))
            {
                summary.gameDistanceMetres = query.value(GameDistanceMetres).toDouble();
            }

            // The protocol 3 figures are written together, so the first one says whether there are any.
            if (!query.isNull(SpeedingSeconds))
            {
                summary.detailed = true;
                summary.speedingSeconds = query.value(SpeedingSeconds).toDouble();
                summary.speedingMetres = query.value(SpeedingMetres).toDouble();
                summary.maxOverLimit = query.value(MaxOverLimit).toDouble();
                summary.cruiseSeconds = query.value(CruiseSeconds).toDouble();
                summary.retarderSeconds = query.value(RetarderSeconds).toDouble();
                summary.motorBrakeSeconds = query.value(MotorBrakeSeconds).toDouble();
                summary.fuelLitres = query.value(FuelLitres).toDouble();
                summary.truckWear = query.value(TruckWear).toDouble();
                summary.cargoDamage = query.value(CargoDamage).toDouble();
            }

            const QJsonArray steering =
                QJsonDocument::fromJson(query.value(SteeringSeconds).toString().toUtf8()).array();

            for (int bin = 0; bin < SessionSummary::SteeringBins && bin < steering.size(); ++bin)
            {
                summary.steeringSeconds.at(static_cast<std::size_t>(bin)) = steering.at(bin).toDouble();
            }

            const QJsonObject gears = QJsonDocument::fromJson(query.value(GearSeconds).toString().toUtf8()).object();

            for (auto it = gears.begin(); it != gears.end(); ++it)
            {
                summary.gearSeconds.insert(it.key().toInt(), it.value().toDouble());
            }

            result.append(summary);
        }

        return result;
    }
} // namespace Wheelstar::Statistics
