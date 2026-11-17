// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"

#include <QtTypes>

namespace Wheelstar::Statistics
{
    /// A repair the player paid for, as the save game's own profit log books it: a bill with upkeep and no kilometres.
    /// The game keeps only the last few days of that log; the database keeps the repairs for good.
    struct RepairRecord
    {
        Telemetry::Game game = Telemetry::Game::Unknown;
        /// The in-game day, the first day being day 1.
        qint64 day = 0;
        /// In the game's currency.
        qint64 amount = 0;
        /// Which of the repairs of the same day and amount it is, 0 for the first: two equal bills on one day are two
        /// repairs, and the same bill read from the next save is still the same one.
        int occurrence = 0;

        friend bool operator==(const RepairRecord&, const RepairRecord&) = default;
    };
} // namespace Wheelstar::Statistics
