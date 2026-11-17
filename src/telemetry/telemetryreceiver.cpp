// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "telemetry/telemetryreceiver.h"
#include "core/logging.h"

#include <QHostAddress>
#include <QNetworkDatagram>
#include <QStandardPaths>

#include <cstring>

using namespace std::chrono_literals;

namespace Wheelstar::Telemetry
{
    bool pluginInstalled()
    {
        return !QStandardPaths::findExecutable(QStringLiteral("istar-wheelstar-ets2-plugin")).isEmpty();
    }

    namespace
    {
        /// A datagram of exactly this struct's size, with its magic and the protocol version this program speaks.
        template <typename T>
        std::optional<T> parse(QByteArrayView data)
        {
            if (data.size() != static_cast<qsizetype>(sizeof(T)))
            {
                return std::nullopt;
            }

            T datagram;
            std::memcpy(&datagram, data.constData(), sizeof(T));

            if (datagram.magic != T{}.magic || datagram.version != ProtocolVersion)
            {
                return std::nullopt;
            }

            return datagram;
        }
    } // namespace

    std::optional<Packet> parsePacket(QByteArrayView data)
    {
        return parse<Packet>(data);
    }

    std::optional<Event> parseEvent(QByteArrayView data)
    {
        auto event = parse<Event>(data);

        if (event)
        {
            // The plugin terminates every text; a datagram from elsewhere might not, and the last byte settles it.
            for (EventText* text :
                 {&event->sourceCity, &event->sourceCompany, &event->destinationCity, &event->destinationCompany,
                  &event->cargo, &event->detail, &event->sourceCityId, &event->sourceCompanyId,
                  &event->destinationCityId, &event->destinationCompanyId, &event->cargoId})
            {
                text->back() = 0;
            }
        }

        return event;
    }

    QString eventText(const EventText& text)
    {
        return QString::fromUtf8(text.data());
    }

    TelemetryReceiver::TelemetryReceiver(QObject* parent) : QObject(parent)
    {
        m_silence.setSingleShot(true);
        m_silence.setInterval(3s);
        connect(&m_silence, &QTimer::timeout, this, &TelemetryReceiver::markSilent);
        connect(&m_socket, &QUdpSocket::readyRead, this, &TelemetryReceiver::readPending);
    }

    bool TelemetryReceiver::start(quint16 port)
    {
        if (!m_socket.bind(QHostAddress::LocalHost, port))
        {
            qCWarning(WHEELSTAR) << "Cannot listen for the game's telemetry on port" << port << m_socket.errorString();
            return false;
        }

        return true;
    }

    bool TelemetryReceiver::isActive() const
    {
        return m_active;
    }

    Game TelemetryReceiver::game() const
    {
        return m_packet.game;
    }

    double TelemetryReceiver::speedKilometresPerHour() const
    {
        // Metres per second to kilometres per hour: 3600 seconds to the hour over 1000 metres to the kilometre.
        return static_cast<double>(m_packet.speed) * 3.6;
    }

    int TelemetryReceiver::gear() const
    {
        return m_packet.gear;
    }

    void TelemetryReceiver::readPending()
    {
        while (m_socket.hasPendingDatagrams())
        {
            const QByteArray data = m_socket.receiveDatagram().data();

            if (const auto event = parseEvent(data))
            {
                Q_EMIT eventReceived(*event);
                continue;
            }

            const auto packet = parsePacket(data);

            if (!packet)
            {
                continue;
            }

            m_packet = *packet;
            m_active = m_packet.paused == 0;
            m_silence.start();
            Q_EMIT packetReceived(m_packet);
            Q_EMIT changed();
        }
    }

    void TelemetryReceiver::markSilent()
    {
        m_active = false;
        Q_EMIT changed();
    }
} // namespace Wheelstar::Telemetry
