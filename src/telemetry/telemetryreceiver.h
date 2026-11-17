// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"

#include <QObject>
#include <QTimer>
#include <QUdpSocket>

#include <optional>

namespace Wheelstar::Telemetry
{
    /// The datagram the plugin sends, or nothing for anything else that arrives on the port.
    std::optional<Packet> parsePacket(QByteArrayView data);
    /// The event datagram the plugin sends, or nothing for anything else.
    std::optional<Event> parseEvent(QByteArrayView data);

    /// A text field of an Event, as a string.
    QString eventText(const EventText& text);

    /// Whether the plugin for Euro Truck Simulator 2 and American Truck Simulator is installed: its package, an
    /// optional one, brings the istar-wheelstar-ets2-plugin command.
    bool pluginInstalled();

    /// Listens for the telemetry plugin that runs inside the game, and keeps the last thing it said.
    class TelemetryReceiver : public QObject
    {
        Q_OBJECT

    public:
        explicit TelemetryReceiver(QObject* parent = nullptr);

        /// Listens on the loopback interface. False when the port is taken, for instance by another instance.
        bool start(quint16 port = DefaultPort);

        /// The game is simulating: packets arrive and it is not paused.
        [[nodiscard]] bool isActive() const;
        [[nodiscard]] Game game() const;
        [[nodiscard]] double speedKilometresPerHour() const;
        /// Positive forward, 0 neutral, negative reverse.
        [[nodiscard]] int gear() const;

    Q_SIGNALS:
        /// Every valid packet, as it arrives.
        void packetReceived(const Wheelstar::Telemetry::Packet& packet);
        /// Something happened in the game: a job, a fine, a toll.
        void eventReceived(const Wheelstar::Telemetry::Event& event);
        /// A packet arrived, or the game stopped sending.
        void changed();

    private:
        void readPending();
        void markSilent();

        QUdpSocket m_socket;
        /// Fires when no packet came for a while: the game closed, or it is paused and sends nothing.
        QTimer m_silence;
        Packet m_packet;
        bool m_active = false;
    };
} // namespace Wheelstar::Telemetry
