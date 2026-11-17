// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace Wheelstar::Statistics
{
    /// One job, delivered or given up: from the telemetry's events, or from the save game's delivery or bus job log.
    struct JobRecord
    {
        enum class Origin
        {
            Telemetry,
            SaveGame,
        };

        enum class Vehicle
        {
            Truck,
            Bus,
        };

        Telemetry::Game game = Telemetry::Game::Unknown;
        Origin origin = Origin::Telemetry;
        Vehicle vehicle = Vehicle::Truck;
        bool delivered = true;

        /// When it was taken and finished, in real time; unknown for a job from the save game.
        QDateTime takenAt;
        QDateTime finishedAt;
        /// In-game minutes since the first in-game day when it was finished: the order of the jobs, and with the
        /// companies and the cargo what tells one apart.
        quint32 gameTime = 0;

        /// The game's ids (a save game has nothing else), and the names in the game's language when the
        /// telemetry knew them.
        QString sourceCityId;
        QString sourceCompanyId;
        QString destinationCityId;
        QString destinationCompanyId;
        QString cargoId;
        QString sourceCity;
        QString sourceCompany;
        QString destinationCity;
        QString destinationCompany;
        QString cargo;
        /// "cargo_market", "freight_market", "quick_job", "external_contracts" and so on, as the game names it.
        QString market;
        /// The truck or the bus, as the save game names it ("vehicle.volvo.fh_2024"); unknown from the telemetry.
        QString truck;

        /// Kilograms.
        double mass = 0;
        double plannedKilometres = 0;
        double drivenKilometres = 0;
        /// The money of the game's currency: what the job paid, or the penalty for giving it up as a negative sum.
        qint64 revenue = 0;
        int experience = 0;
        /// 0 to 1.
        double cargoDamage = 0;
        /// In-game minutes from taking it to delivering it.
        quint32 deliveryMinutes = 0;
        /// Fines paid during the job, as the save game counts them.
        qint64 fines = 0;
        bool autoParked = false;
        bool autoLoaded = false;
        bool special = false;

        /// A bus job's passengers over all its stops, the stops, and how content the passengers were (0 to 1).
        int passengers = 0;
        int stops = 0;
        double satisfaction = 0;

        /// "truck" or "bus", as the database and the JSON keep it.
        [[nodiscard]] QString vehicleKey() const;
        static Vehicle vehicleFromKey(const QString& key);

        [[nodiscard]] QJsonObject toJson() const;
        static std::optional<JobRecord> fromJson(const QJsonObject& object);
    };
} // namespace Wheelstar::Statistics
