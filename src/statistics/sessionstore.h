// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "savegame/saveimport.h"
#include "statistics/gameeventrecord.h"
#include "statistics/jobrecord.h"
#include "statistics/sessionsummary.h"

#include <QHash>
#include <QList>
#include <QString>

#include <expected>

namespace Wheelstar::Statistics
{
    /// Where the sessions are kept: a PostgreSQL server, or a local SQLite file.
    struct DatabaseConnection
    {
        /// "QPSQL" or "QSQLITE".
        QString driver;
        QString host;
        int port = 5432;
        /// The database for PostgreSQL, the file for SQLite.
        QString name;
        QString user;
        /// Empty for PostgreSQL leaves it to libpq, which reads ~/.pgpass.
    };

    /// Keeps the session summaries, the jobs and the costs on the road in a database, and what cannot be written -
    /// the server is not reachable - in a file next to it, to try again later.
    ///
    /// Every call opens its own connection and closes it again: sessions are rare, and a connection held for
    /// hours is one the server or the network may have dropped.
    class SessionStore
    {
    public:
        SessionStore(DatabaseConnection connection, QString pendingDirectory);

        /// Writes the session. When the database cannot be reached it is kept in the pending directory, and false
        /// is returned. A session that is already there - same game, same start - is not written twice.
        bool save(const SessionSummary& summary);
        /// The same for a job; the same job twice (same end in game time, same companies and cargo) is one row.
        bool saveJob(const JobRecord& job);
        bool saveEvent(const GameEventRecord& event);

        /// Writes many jobs in one transaction, those already there left out. How many were new, or -1 when the
        /// database could not be written; nothing is kept for later.
        [[nodiscard]] int writeJobs(const QList<JobRecord>& jobs) const;

        /// Writes what could not be written before. Returns how many are still waiting.
        int flushPending();

        /// The newest sessions first, or why the database could not be read.
        [[nodiscard]] std::expected<QList<SessionSummary>, QString> sessions(int limit = 200) const;
        /// The latest jobs first, by the game's clock. The database names the cities, companies and cargoes from
        /// game_locale: in this language ("de_de", or any "de_…" the game has), else as the telemetry named them,
        /// else as the game's definition does; a job keeps its ids where none of them is known.
        [[nodiscard]] std::expected<QList<JobRecord>, QString> jobs(int limit = 2000,
                                                                    const QString& language = {}) const;

        /// What the archives of a game were when its names were last written (GameData::archivesFingerprint);
        /// empty when they never were or the database cannot be read.
        [[nodiscard]] QString gameLocaleFingerprint(Telemetry::Game game) const;
        /// Replaces a game's names, in one transaction, and keeps the fingerprint they were read at.
        [[nodiscard]] bool writeGameLocale(Telemetry::Game game, const QString& fingerprint,
                                           const QList<GameData::GameName>& names) const;
        /// The game's names of cities, countries, companies and cargoes by kind ("city", …), then id: in this
        /// language ("de_de", else any "de_…" the game has), else the definition's own. What the save game only knows
        /// by id - a driver's hometown, a garage's city - is named from here.
        [[nodiscard]] std::expected<QHash<QString, QHash<QString, QString>>, QString>
        gameNames(Telemetry::Game game, const QString& language) const;
        /// The latest fines, tolls and crossings first.
        [[nodiscard]] std::expected<QList<GameEventRecord>, QString> events(int limit = 2000) const;

    private:
        [[nodiscard]] bool write(const SessionSummary& summary) const;
        [[nodiscard]] bool writeEvent(const GameEventRecord& event) const;
        void keepForLater(const QJsonObject& object, const QDateTime& when, const QString& kind) const;

        DatabaseConnection m_connection;
        QString m_pendingDirectory;
    };
} // namespace Wheelstar::Statistics
