// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"

#include <QDateTime>
#include <QJsonObject>
#include <QMap>

#include <array>
#include <optional>

namespace Wheelstar::Statistics
{
    /// How one drive was driven, in aggregates: the samples themselves are not kept.
    struct SessionSummary
    {
        /// The steering range from full left to full right, in bins of equal width.
        static constexpr int SteeringBins = 20;

        Telemetry::Game game = Telemetry::Game::Unknown;
        QDateTime startedAt;
        QDateTime endedAt;

        /// Seconds the game was simulating, that is not paused.
        double drivingSeconds = 0;
        /// Of those, the seconds the truck moved faster than walking pace.
        double movingSeconds = 0;
        /// Speed times time: the distance covered on the game's scaled-down map. The average speed comes from it.
        double distanceMetres = 0;
        /// The distance as the game shows it, from the truck's odometer. Nothing when the plugin sent no odometer,
        /// which is every drive recorded before it did.
        std::optional<double> gameDistanceMetres;
        /// Metres per second.
        double maxSpeed = 0;

        /// Seconds moving, by steering position, left to right.
        std::array<double, SteeringBins> steeringSeconds{};
        /// Reversals of the steering direction that were large enough to be more than hand tremor.
        int steeringCorrections = 0;

        /// Seconds moving with the throttle released, and with it floored.
        double throttleIdleSeconds = 0;
        double throttleFullSeconds = 0;
        /// Seconds moving with the brake pressed, and the number of times it was hit hard at speed.
        double brakeSeconds = 0;
        int hardBrakes = 0;

        /// Seconds in each gear; negative is reverse, 0 neutral.
        QMap<int, double> gearSeconds;

        /// Whether the figures below were recorded: they came with protocol 3, older drives have none of them.
        bool detailed = false;
        /// Seconds and metres moving faster than the speed limit plus a margin, and the largest excess (m/s).
        double speedingSeconds = 0;
        double speedingMetres = 0;
        double maxOverLimit = 0;
        /// Seconds moving with the cruise control on, the retarder on, the engine brake on.
        double cruiseSeconds = 0;
        double retarderSeconds = 0;
        double motorBrakeSeconds = 0;
        /// Litres burnt, refuelling left out.
        double fuelLitres = 0;
        /// How much the truck's wear rose, as the mean of its parts, and the cargo's damage, each 0 to 1.
        double truckWear = 0;
        double cargoDamage = 0;

        /// Metres per second while moving.
        [[nodiscard]] double averageSpeed() const;

        [[nodiscard]] QJsonObject toJson() const;
        /// Nothing for an object that is not a summary.
        static std::optional<SessionSummary> fromJson(const QJsonObject& object);
    };
} // namespace Wheelstar::Statistics
