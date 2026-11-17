// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"
#include "statistics/jobrecord.h"

#include "truckdata/gamedata/gamenames.h"
#include "truckdata/savegame/career.h"
#include "truckdata/savegame/savegamefile.h"
#include "truckdata/savegame/savegamelocator.h"

#include <QList>
#include <QString>

/// Wheelstar's side of istar-truckdata, which reads the save games and the game archives: the library's names where
/// Wheelstar uses them, and what turns its results into Wheelstar's own - job records, translated errors, the game.
namespace Wheelstar
{
    namespace GameData = TruckData::GameData;

    namespace SaveGame
    {
        using TruckData::SaveGame::Career;
        using TruckData::SaveGame::careerFrom;
        using TruckData::SaveGame::findGames;
        using TruckData::SaveGame::findProfiles;
        using TruckData::SaveGame::GameInstall;
        using TruckData::SaveGame::readSiiFile;
        using TruckData::SaveGame::SaveGameProfile;
        using TruckData::SaveGame::SiiDocument;
        using TruckData::SaveGame::SiiError;
        using TruckData::SaveGame::steamRoots;

        /// The library's game as the telemetry's: the same numbers, which both store.
        Telemetry::Game telemetryGame(TruckData::Game game);

        /// Why a save game cannot be read, in the application's language.
        QString errorText(const SiiError& error);

        /// The save game's delivery log and bus job log as job records of this game, oldest first.
        QList<Statistics::JobRecord> deliveriesFrom(const SiiDocument& document, Telemetry::Game game);

        /// The deliveries not among the jobs already known. The database already refuses a job with the same end
        /// in game time, companies and cargo; this also catches the telemetry's record of a delivery whose game time
        /// is a few minutes off the save game's, which would otherwise be a second row.
        QList<Statistics::JobRecord> newDeliveries(const QList<Statistics::JobRecord>& deliveries,
                                                   const QList<Statistics::JobRecord>& known);
    } // namespace SaveGame
} // namespace Wheelstar
