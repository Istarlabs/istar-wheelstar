// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionrecorder.h"

using namespace std::chrono_literals;

namespace Wheelstar::Statistics
{
    SessionRecorder::SessionRecorder(QObject* parent) : QObject(parent)
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(10min);
        connect(&m_idle, &QTimer::timeout, this, &SessionRecorder::finish);
    }

    void SessionRecorder::add(const Telemetry::Packet& packet)
    {
        if (packet.closing != 0)
        {
            finish();
            return;
        }

        if (!m_recording)
        {
            // A game that starts paused, or sits in a menu, is not a session yet.
            if (packet.paused != 0)
            {
                return;
            }

            m_recording = true;
            m_game = packet.game;
            m_startedAt = QDateTime::currentDateTimeUtc();
            m_clock.invalidate();
        }

        // The time since the previous packet, from a monotonic clock whose count is in nanoseconds (1e9 to a second).
        // The first packet has no predecessor and adds nothing.
        const double seconds = m_clock.isValid() ? static_cast<double>(m_clock.nsecsElapsed()) / 1e9 : 0.0;
        m_clock.restart();
        m_idle.start();
        m_accumulator.add(packet, seconds);
    }

    bool SessionRecorder::isRecording() const
    {
        return m_recording;
    }

    void SessionRecorder::finish()
    {
        if (!m_recording)
        {
            return;
        }

        m_idle.stop();
        m_recording = false;

        SessionSummary summary = m_accumulator.summary();
        summary.game = m_game;
        summary.startedAt = m_startedAt;
        summary.endedAt = QDateTime::currentDateTimeUtc();
        m_accumulator = SessionAccumulator{};

        if (summary.drivingSeconds >= MinimumDrivingSeconds)
        {
            Q_EMIT sessionFinished(summary);
        }
    }
} // namespace Wheelstar::Statistics
