// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "statistics/sessionaccumulator.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace Wheelstar::Statistics
{
    /// Follows the telemetry packets, and hands out one summary for every drive.
    ///
    /// A session begins with the first packet of a simulating game. A pause does not end it, or one drive would
    /// fall into pieces at every look at the map. It ends when the plugin says the game is closing, or when no
    /// packet came for ten minutes, which is what a crashed game looks like.
    class SessionRecorder : public QObject
    {
        Q_OBJECT

    public:
        /// A session shorter than this is not kept: a look into the menu, or a game that stopped at once.
        static constexpr double MinimumDrivingSeconds = 30;

        explicit SessionRecorder(QObject* parent = nullptr);

        /// Feeds one packet; connect it to the receiver.
        void add(const Telemetry::Packet& packet);

        [[nodiscard]] bool isRecording() const;

    Q_SIGNALS:
        void sessionFinished(const Wheelstar::Statistics::SessionSummary& summary);

    private:
        void finish();

        SessionAccumulator m_accumulator;
        QDateTime m_startedAt;
        Telemetry::Game m_game = Telemetry::Game::Unknown;
        bool m_recording = false;
        QElapsedTimer m_clock;
        QTimer m_idle;
    };
} // namespace Wheelstar::Statistics
