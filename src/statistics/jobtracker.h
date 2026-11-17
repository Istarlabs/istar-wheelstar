// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "statistics/gameeventrecord.h"
#include "statistics/jobrecord.h"

#include <QDateTime>

#include <optional>
#include <variant>

namespace Wheelstar::Statistics
{
    /// Turns the plugin's events into records: a job from the moment it is taken to its delivery or
    /// cancellation, and fines, tolls and crossings as they come. Pure bookkeeping: the caller says what time it is.
    class JobTracker
    {
    public:
        using Record = std::variant<std::monostate, JobRecord, GameEventRecord>;

        /// The record the event completes, if any: a JobRecord for a delivery or a cancellation, a
        /// GameEventRecord for a fine, a toll, a ferry or a train, nothing (monostate) for a job taken.
        Record add(const Telemetry::Event& event, const QDateTime& now);

    private:
        /// The job taken and not yet finished; nothing when the program started during one it did not see begin.
        std::optional<JobRecord> m_open;
    };
} // namespace Wheelstar::Statistics
