// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionaccumulator.h"

#include <QTest>

#include <array>
#include <cmath>

using namespace Wheelstar::Statistics;
using Wheelstar::Telemetry::Game;
using Wheelstar::Telemetry::Packet;

namespace
{
    Packet driving(float speed = 10.0F, float steering = 0.0F)
    {
        Packet packet;
        packet.game = Game::EuroTruckSimulator2;
        packet.speed = speed;
        packet.steering = steering;
        packet.gear = 3;
        return packet;
    }
} // namespace

class SessionAccumulatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void distanceAndMovingTime()
    {
        SessionAccumulator accumulator;

        for (int i = 0; i < 10; ++i)
        {
            accumulator.add(driving(10.0F), 0.1);
        }

        QCOMPARE(accumulator.summary().drivingSeconds, 1.0);
        QCOMPARE(accumulator.summary().movingSeconds, 1.0);
        QCOMPARE(accumulator.summary().distanceMetres, 10.0);
        QCOMPARE(accumulator.summary().averageSpeed(), 10.0);
        QCOMPARE(accumulator.summary().maxSpeed, 10.0);
    }

    void theGameDistanceComesFromTheOdometer()
    {
        SessionAccumulator accumulator;

        // 100.0 -> 100.5 km in steps of 0.1, then another truck (its odometer 2000 km further), a pause, and a
        // last 0.2 km after it. Neither the jump nor the change across the pause counts.
        for (const float odometer : {100.0F, 100.1F, 100.2F, 100.3F, 100.4F, 100.5F, 2100.5F, 2100.6F})
        {
            Packet packet = driving(10.0F);
            packet.odometer = odometer;
            accumulator.add(packet, 0.1);
        }

        Packet paused = driving(0.0F);
        paused.paused = 1;
        accumulator.add(paused, 0.1);

        for (const float odometer : {2150.0F, 2150.2F})
        {
            Packet packet = driving(10.0F);
            packet.odometer = odometer;
            accumulator.add(packet, 0.1);
        }

        // 0.5 + 0.1 + 0.2 km; the float steps of the odometer leave a few centimetres.
        QVERIFY(accumulator.summary().gameDistanceMetres.has_value());
        QVERIFY(std::abs(*accumulator.summary().gameDistanceMetres - 800.0) < 1.0);
    }

    void speedingCountsBeyondTheMargin()
    {
        SessionAccumulator accumulator;

        // 80 km/h limit (22.2 m/s): 23 m/s is within the 5 km/h margin, 25 m/s is over; no limit known is not.
        for (const float speed : {23.0F, 25.0F, 25.0F})
        {
            Packet packet = driving(speed);
            packet.speedLimit = 22.2F;
            accumulator.add(packet, 0.1);
        }

        accumulator.add(driving(40.0F), 0.1);

        QVERIFY(qFuzzyCompare(accumulator.summary().speedingSeconds, 0.2));
        QVERIFY(qFuzzyCompare(accumulator.summary().speedingMetres, 5.0));
        QVERIFY(std::abs(accumulator.summary().maxOverLimit - 2.8) < 1e-5);
    }

    void fuelWearAndAids()
    {
        SessionAccumulator accumulator;

        // 400 -> 399.5 l burnt, a refill to 600 adds nothing, then 0.2 l more. Wear rises by 0.01 on every part.
        const std::array<float, 4> fuel{400.0F, 399.5F, 600.0F, 599.8F};
        const std::array<float, 4> wear{0.10F, 0.11F, 0.11F, 0.11F};

        for (std::size_t i = 0; i < fuel.size(); ++i)
        {
            Packet packet = driving(10.0F);
            packet.fuel = fuel.at(i);
            packet.wearEngine = packet.wearTransmission = packet.wearCabin = packet.wearChassis = packet.wearWheels =
                wear.at(i);
            packet.cruiseControl = i < 2 ? 22.0F : 0.0F;
            packet.retarderLevel = i == 3 ? 2 : 0;
            accumulator.add(packet, 0.1);
        }

        const SessionSummary& summary = accumulator.summary();

        QVERIFY(summary.detailed);
        QVERIFY(std::abs(summary.fuelLitres - 0.7) < 1e-3);
        QVERIFY(std::abs(summary.truckWear - 0.01) < 1e-5);
        QVERIFY(qFuzzyCompare(summary.cruiseSeconds, 0.2));
        QVERIFY(qFuzzyCompare(summary.retarderSeconds, 0.1));
    }

    void noOdometerIsAnUnknownDistance()
    {
        SessionAccumulator accumulator;
        accumulator.add(driving(10.0F), 0.1);
        accumulator.add(driving(10.0F), 0.1);

        QVERIFY(!accumulator.summary().gameDistanceMetres.has_value());
    }

    void standingStillIsDrivingTimeButNotMoving()
    {
        SessionAccumulator accumulator;
        accumulator.add(driving(0.0F), 0.2);

        QCOMPARE(accumulator.summary().drivingSeconds, 0.2);
        QCOMPARE(accumulator.summary().movingSeconds, 0.0);
        QCOMPARE(accumulator.summary().averageSpeed(), 0.0);
    }

    void aPausedPacketAddsNothing()
    {
        SessionAccumulator accumulator;
        Packet paused = driving(10.0F);
        paused.paused = 1;
        accumulator.add(paused, 0.1);

        QCOMPARE(accumulator.summary().drivingSeconds, 0.0);
        QCOMPARE(accumulator.summary().distanceMetres, 0.0);
    }

    void aStallIsNotDrivingTime()
    {
        SessionAccumulator accumulator;
        accumulator.add(driving(10.0F), 5.0);

        QCOMPARE(accumulator.summary().drivingSeconds, SessionAccumulator::LongestStep);
    }

    void secondsPerGear()
    {
        SessionAccumulator accumulator;
        accumulator.add(driving(), 0.1);
        accumulator.add(driving(), 0.1);

        QCOMPARE(accumulator.summary().gearSeconds.value(3), 0.2);
    }

    void steeringLeftIsOnTheLeftOfTheHistogram()
    {
        // The game counts counterclockwise: positive is left, which is the lower half of the histogram.
        SessionAccumulator accumulator;
        accumulator.add(driving(10.0F, 0.5F), 0.1);

        QCOMPARE(accumulator.summary().steeringSeconds.at(5), 0.1);
    }

    void aReversalIsACorrectionTremorIsNot()
    {
        SessionAccumulator corrections;

        for (const float steering : {0.0F, 0.2F, 0.0F, 0.2F, 0.0F})
        {
            corrections.add(driving(10.0F, steering), 0.05);
        }

        // Out to the right is no correction; the three turns back and forth are.
        QCOMPARE(corrections.summary().steeringCorrections, 3);

        SessionAccumulator tremor;

        // Half the threshold: the 0.01 of CorrectionThreshold itself would sit on the rounding of a float.
        for (const float steering : {0.0F, 0.005F, 0.0F, 0.005F, 0.0F})
        {
            tremor.add(driving(10.0F, steering), 0.05);
        }

        QCOMPARE(tremor.summary().steeringCorrections, 0);
    }

    void aHardBrakeIsCountedOncePerPress()
    {
        SessionAccumulator accumulator;
        Packet braking = driving(10.0F);
        braking.brake = 0.9F;

        for (int i = 0; i < 3; ++i)
        {
            accumulator.add(braking, 0.05);
        }

        accumulator.add(driving(10.0F), 0.05);

        for (int i = 0; i < 3; ++i)
        {
            accumulator.add(braking, 0.05);
        }

        QCOMPARE(accumulator.summary().hardBrakes, 2);
    }

    void aSummarySurvivesJson()
    {
        SessionAccumulator accumulator;
        accumulator.add(driving(12.0F, 0.3F), 0.1);

        SessionSummary summary = accumulator.summary();
        summary.game = Game::AmericanTruckSimulator;
        summary.startedAt = QDateTime(QDate(2026, 11, 12), QTime(8, 30), QTimeZone::UTC);
        summary.endedAt = summary.startedAt.addSecs(3600);

        const auto restored = SessionSummary::fromJson(summary.toJson());

        QVERIFY(restored.has_value());
        QCOMPARE(restored->game, Game::AmericanTruckSimulator);
        QCOMPARE(restored->startedAt, summary.startedAt);
        QCOMPARE(restored->endedAt, summary.endedAt);
        QCOMPARE(restored->distanceMetres, summary.distanceMetres);
        QVERIFY(!restored->gameDistanceMetres.has_value());
        summary.gameDistanceMetres = 912345.0;
        QCOMPARE(SessionSummary::fromJson(summary.toJson())->gameDistanceMetres, std::optional<double>(912345.0));
        QVERIFY(restored->detailed);
        QCOMPARE(restored->fuelLitres, summary.fuelLitres);
        QCOMPARE(restored->steeringSeconds, summary.steeringSeconds);
        QCOMPARE(restored->gearSeconds, summary.gearSeconds);

        QVERIFY(!SessionSummary::fromJson({}).has_value());
    }
};

QTEST_GUILESS_MAIN(SessionAccumulatorTest)
#include "sessionaccumulatortest.moc"
