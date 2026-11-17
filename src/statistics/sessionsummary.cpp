// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/sessionsummary.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace Wheelstar::Statistics
{
    double SessionSummary::averageSpeed() const
    {
        // Distance over the time spent moving, not over the whole drive: waiting at a light or at a stop would
        // otherwise drag the average down, and it says how fast the truck was driven, not how the trip went.
        return movingSeconds > 0 ? distanceMetres / movingSeconds : 0.0;
    }

    QJsonObject SessionSummary::toJson() const
    {
        QJsonArray steering;

        for (const double seconds : steeringSeconds)
        {
            steering.append(seconds);
        }

        QJsonObject gears;

        for (const auto [gear, seconds] : gearSeconds.asKeyValueRange())
        {
            gears.insert(QString::number(gear), seconds);
        }

        QJsonObject object{
            {u"game"_s, static_cast<int>(game)},
            {u"startedAt"_s, startedAt.toString(Qt::ISODateWithMs)},
            {u"endedAt"_s, endedAt.toString(Qt::ISODateWithMs)},
            {u"drivingSeconds"_s, drivingSeconds},
            {u"movingSeconds"_s, movingSeconds},
            {u"distanceMetres"_s, distanceMetres},
            {u"maxSpeed"_s, maxSpeed},
            {u"steeringSeconds"_s, steering},
            {u"steeringCorrections"_s, steeringCorrections},
            {u"throttleIdleSeconds"_s, throttleIdleSeconds},
            {u"throttleFullSeconds"_s, throttleFullSeconds},
            {u"brakeSeconds"_s, brakeSeconds},
            {u"hardBrakes"_s, hardBrakes},
            {u"gearSeconds"_s, gears},
        };

        if (gameDistanceMetres)
        {
            object.insert(u"gameDistanceMetres"_s, *gameDistanceMetres);
        }

        if (detailed)
        {
            object.insert(u"details"_s,
                          QJsonObject{
                              {u"speedingSeconds"_s, speedingSeconds},
                              {u"speedingMetres"_s, speedingMetres},
                              {u"maxOverLimit"_s, maxOverLimit},
                              {u"cruiseSeconds"_s, cruiseSeconds},
                              {u"retarderSeconds"_s, retarderSeconds},
                              {u"motorBrakeSeconds"_s, motorBrakeSeconds},
                              {u"fuelLitres"_s, fuelLitres},
                              {u"truckWear"_s, truckWear},
                              {u"cargoDamage"_s, cargoDamage},
                          });
        }

        return object;
    }

    std::optional<SessionSummary> SessionSummary::fromJson(const QJsonObject& object)
    {
        const QJsonArray steering = object.value(u"steeringSeconds"_s).toArray();

        if (!object.contains(u"startedAt"_s) || steering.size() != SteeringBins)
        {
            return std::nullopt;
        }

        SessionSummary summary;
        summary.game = static_cast<Telemetry::Game>(object.value(u"game"_s).toInt());
        summary.startedAt = QDateTime::fromString(object.value(u"startedAt"_s).toString(), Qt::ISODateWithMs);
        summary.endedAt = QDateTime::fromString(object.value(u"endedAt"_s).toString(), Qt::ISODateWithMs);
        summary.drivingSeconds = object.value(u"drivingSeconds"_s).toDouble();
        summary.movingSeconds = object.value(u"movingSeconds"_s).toDouble();
        summary.distanceMetres = object.value(u"distanceMetres"_s).toDouble();

        if (object.contains(u"gameDistanceMetres"_s))
        {
            summary.gameDistanceMetres = object.value(u"gameDistanceMetres"_s).toDouble();
        }

        if (object.contains(u"details"_s))
        {
            const QJsonObject details = object.value(u"details"_s).toObject();
            summary.detailed = true;
            summary.speedingSeconds = details.value(u"speedingSeconds"_s).toDouble();
            summary.speedingMetres = details.value(u"speedingMetres"_s).toDouble();
            summary.maxOverLimit = details.value(u"maxOverLimit"_s).toDouble();
            summary.cruiseSeconds = details.value(u"cruiseSeconds"_s).toDouble();
            summary.retarderSeconds = details.value(u"retarderSeconds"_s).toDouble();
            summary.motorBrakeSeconds = details.value(u"motorBrakeSeconds"_s).toDouble();
            summary.fuelLitres = details.value(u"fuelLitres"_s).toDouble();
            summary.truckWear = details.value(u"truckWear"_s).toDouble();
            summary.cargoDamage = details.value(u"cargoDamage"_s).toDouble();
        }

        summary.maxSpeed = object.value(u"maxSpeed"_s).toDouble();
        summary.steeringCorrections = object.value(u"steeringCorrections"_s).toInt();
        summary.throttleIdleSeconds = object.value(u"throttleIdleSeconds"_s).toDouble();
        summary.throttleFullSeconds = object.value(u"throttleFullSeconds"_s).toDouble();
        summary.brakeSeconds = object.value(u"brakeSeconds"_s).toDouble();
        summary.hardBrakes = object.value(u"hardBrakes"_s).toInt();

        for (int bin = 0; bin < SteeringBins; ++bin)
        {
            summary.steeringSeconds.at(bin) = steering.at(bin).toDouble();
        }

        const QJsonObject gears = object.value(u"gearSeconds"_s).toObject();

        for (auto it = gears.begin(); it != gears.end(); ++it)
        {
            summary.gearSeconds.insert(it.key().toInt(), it.value().toDouble());
        }

        return summary;
    }
} // namespace Wheelstar::Statistics
