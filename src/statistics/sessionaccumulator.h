// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "statistics/sessionsummary.h"

namespace Wheelstar::Statistics
{
    /// Turns the telemetry packets of one session into its summary. Pure arithmetic: it knows neither a clock nor
    /// a socket, the caller says how much time passed between two packets.
    class SessionAccumulator
    {
    public:
        // None of the thresholds below is measured or taken from the games: they are chosen values, picked so the
        // numbers match what driving feels like. Change one here and only drives recorded afterwards use it; the
        // samples are not stored, so older sessions cannot be recounted.

        /// Below this speed, in metres per second, the truck counts as standing. 1 m/s is 3.6 km/h, a slow walk:
        /// slower than that is a truck waiting at a light or rolling back and forth while parking.
        static constexpr double MovingSpeed = 1.0;
        /// A steering reversal smaller than this fraction of the full lock is jitter, not a correction.
        ///
        /// The steering value runs from -1 to 1 over the whole lock, so the threshold is a share of one side: on a
        /// 900 degree wheel (450 degrees each way) 0.01 is 4.5 degrees. The first real drive showed that the small
        /// lane-keeping movements on a motorway are about 3 degrees (0.0067), and a threshold of 0.05 (22.5 degrees)
        /// counted none of them. 0.01 is just above that, so force feedback jitter and sensor noise stay out.
        static constexpr double CorrectionThreshold = 0.01;
        /// The brake pressed beyond this fraction (of its travel, 0 to 1) at more than HardBrakeSpeed is a hard
        /// brake: a deliberate stomp, as opposed to the light pressure of slowing for a bend.
        static constexpr double HardBrake = 0.8;
        /// 5 m/s is 18 km/h. Below it a full brake is just stopping the truck, not an emergency.
        static constexpr double HardBrakeSpeed = 5.0;
        /// The throttle (0 to 1) below and above these fractions counts as released and as floored. A margin of 5%
        /// on each end keeps a resting foot and the pedal's play from counting as "pressed" or "not floored".
        static constexpr double ThrottleReleased = 0.05;
        static constexpr double ThrottleFloored = 0.95;
        /// What a gap between two packets is clamped to, so a stall does not count as driving time. The plugin sends
        /// one packet per frame, about 60 a second, so a normal gap is 0.017 s. 0.25 s is some fifteen frames: more
        /// than that is the game loading or the window being dragged, not driving.
        static constexpr double LongestStep = 0.25;
        /// A change of the odometer between two packets beyond this many kilometres is not driving: another truck
        /// (each has its own odometer) or a jump across the map. The longest step at 150 km/h on the roads' 1:19 is
        /// some 200 metres, so 1 km leaves room and still catches a jump.
        static constexpr double OdometerJump = 1.0;
        /// Faster than the speed limit by more than this (m/s) is speeding: 1.39 m/s is 5 km/h, the margin a
        /// speedometer and a camera leave, so holding the limit on the dot does not count.
        static constexpr double SpeedingMargin = 5.0 / 3.6;
        /// Fuel dropping by more than this many litres between two packets is not burnt: another truck, or a
        /// truck that was repaired and refilled. A truck burns well under a litre in the longest step.
        static constexpr double FuelJump = 5.0;
        /// Wear or cargo damage rising by more than this (0 to 1) between two packets is another truck or job,
        /// not driving; a crash adds a few percent at once and still counts.
        static constexpr double DamageJump = 0.25;

        /// `seconds` is the time since the previous packet. A paused packet adds nothing and ends the stretch the
        /// pedal and steering edges are tracked over.
        void add(const Telemetry::Packet& packet, double seconds);

        [[nodiscard]] const SessionSummary& summary() const;

    private:
        /// A value that only rises while driving (the odometer, wear, damage, fuel burnt): how much it rose since the
        /// previous packet. A first value, a fall or a rise of `jump` or more adds nothing; 0 or less is "no value"
        /// (a channel the game does not offer) and leaves the previous one in place.
        static double rise(std::optional<double>& previous, double value, double jump);

        void trackSteering(double steering);
        void trackGauges(const Telemetry::Packet& packet, bool moving, double speed, double step);

        SessionSummary m_summary;

        // The steering reversals: the direction the wheel is turning (0 before it moved far enough to tell), and
        // the furthest it got in that direction.
        bool m_steeringSeen = false;
        int m_direction = 0;
        double m_extreme = 0;
        bool m_braking = false;

        // The gauges of the previous packet; nothing at the start and after a pause. Fuel is kept negated, so what
        // is burnt is a rise like the others.
        std::optional<double> m_odometer;
        std::optional<double> m_fuel;
        std::optional<double> m_wear;
        std::optional<double> m_cargoDamage;
    };
} // namespace Wheelstar::Statistics
