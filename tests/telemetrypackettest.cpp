// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "telemetry/telemetryreceiver.h"

#include <QTest>

#include <cstring>

using namespace Wheelstar::Telemetry;

class TelemetryPacketTest : public QObject
{
    Q_OBJECT

private:
    template <typename T>
    static QByteArray bytesOf(const T& datagram)
    {
        QByteArray bytes(static_cast<qsizetype>(sizeof(T)), 0);
        std::memcpy(bytes.data(), &datagram, sizeof(T));
        return bytes;
    }

private Q_SLOTS:
    void aPacketSurvivesTheWire()
    {
        Packet sent;
        sent.game = Game::EuroTruckSimulator2;
        sent.speed = 24.5F;
        sent.gear = -1;
        sent.steering = -0.25F;
        sent.brake = 1;
        sent.odometer = 123456.5F;
        sent.gameTime = 406437;
        sent.speedLimit = 22.2F;
        sent.wearWheels = 0.05F;
        sent.motorBrake = 1;

        const auto received = parsePacket(bytesOf(sent));

        QVERIFY(received.has_value());
        QCOMPARE(received->game, Game::EuroTruckSimulator2);
        QCOMPARE(received->speed, 24.5F);
        QCOMPARE(received->gear, -1);
        QCOMPARE(received->steering, -0.25F);
        QCOMPARE(received->brake, 1.0F);
        QCOMPARE(received->odometer, 123456.5F);
        QCOMPARE(received->gameTime, 406437U);
        QCOMPARE(received->speedLimit, 22.2F);
        QCOMPARE(received->wearWheels, 0.05F);
        QCOMPARE(received->motorBrake, 1);
        // An event is not a frame, nor the other way round.
        QVERIFY(!parseEvent(bytesOf(sent)).has_value());
    }

    void anEventSurvivesTheWire()
    {
        Event sent;
        sent.kind = EventKind::Fined;
        sent.amount = 1200;
        sent.gameTime = 1000;
        const QByteArray offence("red_signal");
        std::memcpy(sent.detail.data(), offence.constData(), static_cast<std::size_t>(offence.size()));

        const auto received = parseEvent(bytesOf(sent));

        QVERIFY(received.has_value());
        QCOMPARE(received->kind, EventKind::Fined);
        QCOMPARE(received->amount, 1200);
        QCOMPARE(eventText(received->detail), QStringLiteral("red_signal"));
        QVERIFY(!parsePacket(bytesOf(sent)).has_value());

        // A text without its terminating zero is cut at the field's end rather than read past it.
        Event unterminated;
        unterminated.cargo.fill('x');
        QCOMPARE(eventText(parseEvent(bytesOf(unterminated))->cargo).size(), 63);
    }

    void theDatagramsHaveTheDocumentedSize()
    {
        QCOMPARE(sizeof(Packet), 88U);
        QCOMPARE(sizeof(Event), 748U);
        QCOMPARE(bytesOf(Packet{}).left(4), QByteArray("OVST"));
        QCOMPARE(bytesOf(Event{}).left(4), QByteArray("OVEV"));
    }

    void somethingElseOnThePortIsIgnored()
    {
        QVERIFY(!parsePacket(QByteArray("hello")).has_value());
        QVERIFY(!parsePacket(QByteArray()).has_value());

        // Right size, wrong magic.
        QByteArray wrong = bytesOf(Packet{});
        wrong[0] = 'X';
        QVERIFY(!parsePacket(wrong).has_value());

        // Right size, a version this program does not know.
        Packet newer;
        newer.version = ProtocolVersion + 1;
        QVERIFY(!parsePacket(bytesOf(newer)).has_value());
    }
};

QTEST_GUILESS_MAIN(TelemetryPacketTest)
#include "telemetrypackettest.moc"
