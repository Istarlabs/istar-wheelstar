// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/jobrecord.h"

using namespace Qt::StringLiterals;

namespace Wheelstar::Statistics
{
    QString JobRecord::vehicleKey() const
    {
        return vehicle == Vehicle::Bus ? u"bus"_s : u"truck"_s;
    }

    JobRecord::Vehicle JobRecord::vehicleFromKey(const QString& key)
    {
        // A job from before the buses has no key, and was a truck's.
        return key == "bus"_L1 ? Vehicle::Bus : Vehicle::Truck;
    }

    QJsonObject JobRecord::toJson() const
    {
        return {
            {u"record"_s, u"job"_s},
            {u"game"_s, static_cast<int>(game)},
            {u"origin"_s, origin == Origin::SaveGame ? u"save"_s : u"telemetry"_s},
            {u"delivered"_s, delivered},
            {u"takenAt"_s, takenAt.toString(Qt::ISODateWithMs)},
            {u"finishedAt"_s, finishedAt.toString(Qt::ISODateWithMs)},
            {u"gameTime"_s, static_cast<qint64>(gameTime)},
            {u"sourceCityId"_s, sourceCityId},
            {u"sourceCompanyId"_s, sourceCompanyId},
            {u"destinationCityId"_s, destinationCityId},
            {u"destinationCompanyId"_s, destinationCompanyId},
            {u"cargoId"_s, cargoId},
            {u"sourceCity"_s, sourceCity},
            {u"sourceCompany"_s, sourceCompany},
            {u"destinationCity"_s, destinationCity},
            {u"destinationCompany"_s, destinationCompany},
            {u"cargo"_s, cargo},
            {u"market"_s, market},
            {u"truck"_s, truck},
            {u"mass"_s, mass},
            {u"plannedKilometres"_s, plannedKilometres},
            {u"drivenKilometres"_s, drivenKilometres},
            {u"revenue"_s, revenue},
            {u"experience"_s, experience},
            {u"cargoDamage"_s, cargoDamage},
            {u"deliveryMinutes"_s, static_cast<qint64>(deliveryMinutes)},
            {u"fines"_s, fines},
            {u"autoParked"_s, autoParked},
            {u"autoLoaded"_s, autoLoaded},
            {u"special"_s, special},
            {u"vehicle"_s, vehicleKey()},
            {u"passengers"_s, passengers},
            {u"stops"_s, stops},
            {u"satisfaction"_s, satisfaction},
        };
    }

    std::optional<JobRecord> JobRecord::fromJson(const QJsonObject& object)
    {
        if (object.value(u"record"_s).toString() != "job"_L1)
        {
            return std::nullopt;
        }

        JobRecord job;
        job.game = static_cast<Telemetry::Game>(object.value(u"game"_s).toInt());
        job.origin = object.value(u"origin"_s).toString() == "save"_L1 ? Origin::SaveGame : Origin::Telemetry;
        job.delivered = object.value(u"delivered"_s).toBool();
        job.takenAt = QDateTime::fromString(object.value(u"takenAt"_s).toString(), Qt::ISODateWithMs);
        job.finishedAt = QDateTime::fromString(object.value(u"finishedAt"_s).toString(), Qt::ISODateWithMs);
        job.gameTime = static_cast<quint32>(object.value(u"gameTime"_s).toInteger());
        job.sourceCityId = object.value(u"sourceCityId"_s).toString();
        job.sourceCompanyId = object.value(u"sourceCompanyId"_s).toString();
        job.destinationCityId = object.value(u"destinationCityId"_s).toString();
        job.destinationCompanyId = object.value(u"destinationCompanyId"_s).toString();
        job.cargoId = object.value(u"cargoId"_s).toString();
        job.sourceCity = object.value(u"sourceCity"_s).toString();
        job.sourceCompany = object.value(u"sourceCompany"_s).toString();
        job.destinationCity = object.value(u"destinationCity"_s).toString();
        job.destinationCompany = object.value(u"destinationCompany"_s).toString();
        job.cargo = object.value(u"cargo"_s).toString();
        job.market = object.value(u"market"_s).toString();
        job.truck = object.value(u"truck"_s).toString();
        job.mass = object.value(u"mass"_s).toDouble();
        job.plannedKilometres = object.value(u"plannedKilometres"_s).toDouble();
        job.drivenKilometres = object.value(u"drivenKilometres"_s).toDouble();
        job.revenue = object.value(u"revenue"_s).toInteger();
        job.experience = object.value(u"experience"_s).toInt();
        job.cargoDamage = object.value(u"cargoDamage"_s).toDouble();
        job.deliveryMinutes = static_cast<quint32>(object.value(u"deliveryMinutes"_s).toInteger());
        job.fines = object.value(u"fines"_s).toInteger();
        job.autoParked = object.value(u"autoParked"_s).toBool();
        job.autoLoaded = object.value(u"autoLoaded"_s).toBool();
        job.special = object.value(u"special"_s).toBool();
        job.vehicle = vehicleFromKey(object.value(u"vehicle"_s).toString());
        job.passengers = object.value(u"passengers"_s).toInt();
        job.stops = object.value(u"stops"_s).toInt();
        job.satisfaction = object.value(u"satisfaction"_s).toDouble();
        return job;
    }
} // namespace Wheelstar::Statistics
