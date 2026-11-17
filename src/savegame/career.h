// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "savegame/siiunit.h"
#include "statistics/jobrecord.h"

#include <QList>
#include <QStringList>

#include <array>

namespace Wheelstar::SaveGame
{
    /// What a save game knows about the whole career, beyond any one drive.
    struct Career
    {
        /// The offences in the order the save game counts them, which is the SDK's order of fine_offence.
        static const QStringList Offences;

        qint64 experience = 0;
        qint64 money = 0;
        /// In-game minutes since the first in-game day.
        qint64 gameTime = 0;

        /// The game's own counters by way of driving: with a truck, with a bus, with a car. Kilometres, minutes
        /// of real time, minutes of game time.
        std::array<qint64, 3> kilometres{};
        std::array<qint64, 3> realMinutes{};
        std::array<qint64, 3> gameMinutes{};

        /// Truck deliveries and bus jobs, each from its own log.
        qint64 deliveries = 0;
        qint64 busJobs = 0;
        qint64 cancelledJobs = 0;
        qint64 visitedCities = 0;
        qint64 fuelLitres = 0;
        qint64 fuelPrice = 0;
        qint64 gasStationVisits = 0;
        qint64 serviceVisits = 0;
        qint64 ferryCrossings = 0;
        qint64 garages = 0;

        /// The longest stretches without a scratch, without cargo damage, without an offence, in kilometres; and
        /// how far the current ones have come.
        qint64 longestClean = 0;
        qint64 currentClean = 0;
        qint64 longestWithoutCargoDamage = 0;
        qint64 longestWithoutOffence = 0;
        qint64 currentWithoutOffence = 0;

        /// Per offence (in the order of Offences): how often the game saw it, also with fines turned off; how
        /// often it was fined; what the fines came to.
        QList<qint64> offencesCommitted;
        QList<qint64> offencesFined;
        QList<qint64> finesPaid;
    };

    /// The career of the save game, or an empty one when it has no economy (not a game save).
    Career careerFrom(const SiiDocument& document);

    /// The delivery log and the bus job log of the save game as jobs, oldest first.
    ///
    /// Each delivery log entry is a list of texts whose meaning is their position, as the community's tools read
    /// it: 0 the game time it ended, 1 and 2 "company.volatile.<company>.<city>" of source and destination, 3
    /// "cargo.<id>", 4 the experience, 5 the revenue, 6 the kilometres, 7 the cargo damage (0 to 1), 14 the
    /// fines, 16 the truck, 18 the job market. Entries without a source (free roaming) are left out.
    ///
    /// A bus job log entry has named values: its stops as "city.<id>", the bus, the reward, the experience, the
    /// kilometres, the start and end in game time, the passengers, the stops and their satisfaction.
    QList<Statistics::JobRecord> deliveriesFrom(const SiiDocument& document, Telemetry::Game game);

    /// The deliveries not among the jobs already known. The database already refuses a job with the same end in
    /// game time, companies and cargo; this also catches the telemetry's record of a delivery whose game time is a
    /// few minutes off the save game's, which would otherwise be a second row.
    QList<Statistics::JobRecord> newDeliveries(const QList<Statistics::JobRecord>& deliveries,
                                               const QList<Statistics::JobRecord>& known);
} // namespace Wheelstar::SaveGame
