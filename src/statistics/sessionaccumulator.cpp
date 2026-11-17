// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionaccumulator.h"

#include <algorithm>
#include <cmath>

namespace Wheelstar::Statistics
{
    void SessionAccumulator::add(const Telemetry::Packet& packet, double seconds)
    {
        if (packet.paused != 0)
        {
            m_steeringSeen = false;
            m_direction = 0;
            m_braking = false;
            m_odometer.reset();
            m_fuel.reset();
            m_wear.reset();
            m_cargoDamage.reset();
            return;
        }

        const double step = std::clamp(seconds, 0.0, LongestStep);
        const double speed = std::abs(static_cast<double>(packet.speed));
        const bool moving = speed > MovingSpeed;

        trackGauges(packet, moving, speed, step);
        m_summary.drivingSeconds += step;
        m_summary.gearSeconds.insert(packet.gear, m_summary.gearSeconds.value(packet.gear) + step);
        m_summary.maxSpeed = std::max(m_summary.maxSpeed, speed);

        if (!moving)
        {
            m_braking = false;
            return;
        }

        m_summary.movingSeconds += step;
        // Distance is speed times time, summed over every packet (m/s * s = m). The reverse gear counts as distance
        // too, because speed is taken as an absolute value above.
        m_summary.distanceMetres += speed * step;

        // The game counts steering counterclockwise; the histogram runs from left to right like the wheel.
        const double steering = std::clamp(-static_cast<double>(packet.steering), -1.0, 1.0);
        // (steering + 1) / 2 moves -1..1 to 0..1, and times the bin count it is the bin number. Exactly 1 (full lock
        // to the right) would land one past the last bin, which is what the min() catches.
        const int bin = std::min(SessionSummary::SteeringBins - 1,
                                 static_cast<int>((steering + 1.0) / 2.0 * SessionSummary::SteeringBins));
        m_summary.steeringSeconds.at(static_cast<std::size_t>(bin)) += step;
        trackSteering(steering);

        if (packet.throttle < ThrottleReleased)
        {
            m_summary.throttleIdleSeconds += step;
        }
        else if (packet.throttle > ThrottleFloored)
        {
            m_summary.throttleFullSeconds += step;
        }

        if (packet.brake > ThrottleReleased)
        {
            m_summary.brakeSeconds += step;
        }

        const bool hard = static_cast<double>(packet.brake) > HardBrake && speed > HardBrakeSpeed;

        // One press is one hard brake however many packets it lasts, so only the packet that starts it counts.
        if (hard && !m_braking)
        {
            ++m_summary.hardBrakes;
        }

        m_braking = hard;
    }

    const SessionSummary& SessionAccumulator::summary() const
    {
        return m_summary;
    }

    double SessionAccumulator::rise(std::optional<double>& previous, double value, double jump)
    {
        if (value <= 0)
        {
            return 0;
        }

        // Summed from packet to packet, not end minus start: a change of truck or a jump would otherwise land in it
        // whole. A float gauge (the odometer at some 100000 km) moves in coarse steps; the steps still add up.
        const double change = previous ? value - *previous : 0.0;
        previous = value;
        return change > 0 && change < jump ? change : 0.0;
    }

    void SessionAccumulator::trackGauges(const Telemetry::Packet& packet, bool moving, double speed, double step)
    {
        // A game without the odometer sends 0, and the distance stays unknown rather than 0.
        if (packet.odometer > 0)
        {
            // 1000 metres to a kilometre.
            m_summary.gameDistanceMetres = m_summary.gameDistanceMetres.value_or(0) +
                (rise(m_odometer, static_cast<double>(packet.odometer), OdometerJump) * 1000);
        }

        m_summary.detailed = true;

        // The tank going down is fuel burnt; negated, that is a rise, and refuelling is a fall that adds nothing.
        // An empty tank (0) would read as "no value", which only loses the last drop.
        if (packet.fuel > 0)
        {
            m_summary.fuelLitres += rise(m_fuel, 1000.0 - static_cast<double>(packet.fuel), FuelJump);
        }

        // The mean of the five parts: a scratch on the cabin and a worn engine count the same.
        const double wear = (static_cast<double>(packet.wearEngine) + packet.wearTransmission + packet.wearCabin +
                             packet.wearChassis + packet.wearWheels) /
            5.0;
        m_summary.truckWear += rise(m_wear, wear, DamageJump);
        m_summary.cargoDamage += rise(m_cargoDamage, static_cast<double>(packet.cargoDamage), DamageJump);

        if (!moving)
        {
            return;
        }

        const auto limit = static_cast<double>(packet.speedLimit);

        // No limit known (0) is no speeding either.
        if (limit > 0 && speed > limit + SpeedingMargin)
        {
            m_summary.speedingSeconds += step;
            m_summary.speedingMetres += speed * step;
            m_summary.maxOverLimit = std::max(m_summary.maxOverLimit, speed - limit);
        }

        if (packet.cruiseControl > 0)
        {
            m_summary.cruiseSeconds += step;
        }

        if (packet.retarderLevel > 0)
        {
            m_summary.retarderSeconds += step;
        }

        if (packet.motorBrake != 0)
        {
            m_summary.motorBrakeSeconds += step;
        }
    }

    void SessionAccumulator::trackSteering(double steering)
    {
        if (!m_steeringSeen)
        {
            m_steeringSeen = true;
            m_direction = 0;
            m_extreme = steering;
            return;
        }

        if (m_direction == 0)
        {
            // The wheel has not moved far enough yet to tell which way it is turning.
            if (std::abs(steering - m_extreme) >= CorrectionThreshold)
            {
                m_direction = steering > m_extreme ? 1 : -1;
                m_extreme = steering;
            }

            return;
        }

        // Turning further the same way moves the extreme along; turning back by enough is a correction.
        if ((steering - m_extreme) * m_direction > 0)
        {
            m_extreme = steering;
        }
        else if (std::abs(steering - m_extreme) >= CorrectionThreshold)
        {
            ++m_summary.steeringCorrections;
            m_direction = -m_direction;
            m_extreme = steering;
        }
    }
} // namespace Wheelstar::Statistics
