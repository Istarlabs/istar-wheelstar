// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/career.h"

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace Wheelstar::SaveGame
{
    const QStringList Career::Offences = {
        u"crash"_s,
        u"avoid_sleeping"_s,
        u"wrong_way"_s,
        u"speeding_camera"_s,
        u"no_lights"_s,
        u"red_signal"_s,
        u"speeding"_s,
        u"avoid_weighing"_s,
        u"illegal_trailer"_s,
        u"avoid_inspection"_s,
        u"illegal_border_crossing"_s,
        u"hard_shoulder_violation"_s,
        u"damaged_vehicle_usage"_s,
        u"generic"_s,
    };

    namespace
    {
        /// "company.volatile.posped.bremen" is the company "posped" in the city "bremen".
        std::pair<QString, QString> companyAndCity(const QString& reference)
        {
            const QStringList parts = reference.split(u'.');

            if (parts.size() < 4)
            {
                return {};
            }

            return {parts.at(2), parts.at(3)};
        }

        /// The first three counters of a "by mode" list, which has one per way of driving.
        std::array<qint64, 3> modes(const SiiUnit& unit, const QString& key)
        {
            const QList<qint64> values = unit.numbers(key);
            std::array<qint64, 3> result{};

            for (qsizetype i = 0; i < values.size() && i < 3; ++i)
            {
                result.at(static_cast<std::size_t>(i)) = values.at(i);
            }

            return result;
        }

        /// Exactly as many entries as there are offences, so a shorter or longer list from another version of the
        /// game does not shift them.
        QList<qint64> perOffence(QList<qint64> values)
        {
            values.resize(Career::Offences.size(), 0);
            return values;
        }
    } // namespace

    Career careerFrom(const SiiDocument& document)
    {
        Career career;
        const SiiUnit* economy = document.first(u"economy"_s);

        if (economy == nullptr)
        {
            return career;
        }

        career.experience = economy->number(u"experience_points"_s);
        career.gameTime = economy->number(u"game_time"_s);
        career.kilometres = modes(*economy, u"total_distances_by_mode"_s);
        career.realMinutes = modes(*economy, u"total_real_times_by_mode"_s);
        career.gameMinutes = modes(*economy, u"total_game_times_by_mode"_s);
        career.cancelledJobs = economy->number(u"cancelled_job_count"_s);
        career.visitedCities = economy->texts(u"visited_cities"_s).size();
        career.fuelLitres = economy->number(u"total_fuel_litres"_s);
        career.fuelPrice = economy->number(u"total_fuel_price"_s);
        career.gasStationVisits = economy->number(u"gas_station_visit_count"_s);
        career.serviceVisits = economy->number(u"service_visit_count"_s);
        career.garages = economy->texts(u"garages"_s).size();
        career.longestClean = economy->number(u"clean_distance_max"_s);
        career.currentClean = economy->number(u"clean_distance_counter"_s);
        career.longestWithoutCargoDamage = economy->number(u"no_cargo_damage_distance_max"_s);
        career.longestWithoutOffence = economy->number(u"no_violation_distance_max"_s);
        career.currentWithoutOffence = economy->number(u"no_violation_distance_counter"_s);

        if (const SiiUnit* bank = document.unit(economy->text(u"bank"_s)))
        {
            career.money = bank->number(u"money_account"_s);
        }

        // The log also has entries for free roaming, without a job; only the jobs count.
        for (const Statistics::JobRecord& job : deliveriesFrom(document, Telemetry::Game::Unknown))
        {
            ++(job.vehicle == Statistics::JobRecord::Vehicle::Bus ? career.busJobs : career.deliveries);
        }

        if (const SiiUnit* ferries = document.unit(economy->text(u"ferry_log"_s)))
        {
            for (const QString& id : ferries->texts(u"entries"_s))
            {
                if (const SiiUnit* entry = document.unit(id))
                {
                    career.ferryCrossings += entry->number(u"use_count"_s);
                }
            }
        }

        if (const SiiUnit* police = document.unit(economy->text(u"police_ctrl"_s)))
        {
            career.offencesCommitted = perOffence(police->numbers(u"offence_counter"_s));
        }

        if (const SiiUnit* log = document.unit(economy->text(u"police_offence_log"_s)))
        {
            career.offencesFined = perOffence(log->numbers(u"offence_total_counts"_s));
            career.finesPaid = perOffence(log->numbers(u"offence_total_fines"_s));
        }

        return career;
    }

    QList<Statistics::JobRecord> deliveriesFrom(const SiiDocument& document, Telemetry::Game game)
    {
        QList<Statistics::JobRecord> jobs;
        const SiiUnit* economy = document.first(u"economy"_s);

        if (economy == nullptr)
        {
            return jobs;
        }

        const SiiUnit* log = document.unit(economy->text(u"delivery_log"_s));

        for (const QString& id : log != nullptr ? log->texts(u"entries"_s) : QStringList())
        {
            const SiiUnit* entry = document.unit(id);
            const QStringList params = entry != nullptr ? entry->texts(u"params"_s) : QStringList();

            if (params.size() < 19 || params.at(1).isEmpty() || params.at(2).isEmpty())
            {
                continue;
            }

            Statistics::JobRecord job;
            job.game = game;
            job.origin = Statistics::JobRecord::Origin::SaveGame;
            job.gameTime = params.at(0).toUInt();
            std::tie(job.sourceCompanyId, job.sourceCityId) = companyAndCity(params.at(1));
            std::tie(job.destinationCompanyId, job.destinationCityId) = companyAndCity(params.at(2));
            job.cargoId = params.at(3).section(u'.', 1);
            job.experience = params.at(4).toInt();
            job.revenue = params.at(5).toLongLong();
            job.drivenKilometres = params.at(6).toDouble();
            job.cargoDamage = params.at(7).toDouble();
            job.fines = params.at(14).toLongLong();
            job.truck = params.at(16);
            job.market = params.at(18);
            jobs.append(job);
        }

        if (const SiiUnit* buses = document.unit(economy->text(u"bus_job_log"_s)))
        {
            for (const QString& id : buses->texts(u"entries"_s))
            {
                const SiiUnit* entry = document.unit(id);

                if (entry == nullptr)
                {
                    continue;
                }

                const qint64 start = entry->number(u"start_time"_s);
                const qint64 end = entry->number(u"end_time"_s);

                Statistics::JobRecord job;
                job.game = game;
                job.origin = Statistics::JobRecord::Origin::SaveGame;
                job.vehicle = Statistics::JobRecord::Vehicle::Bus;
                // ponytail: every entry seen so far has flags 1 and was finished; a given-up bus job may look
                // different, read the flags once one shows up.
                job.gameTime = static_cast<quint32>(end);
                job.deliveryMinutes = static_cast<quint32>(std::max<qint64>(end - start, 0));
                // "city.zurich" is the stop in Zürich.
                job.sourceCityId = entry->text(u"source_bus_stop_name"_s).section(u'.', 1);
                job.destinationCityId = entry->text(u"target_bus_stop_name"_s).section(u'.', 1);
                job.truck = entry->text(u"assigned_bus"_s);
                job.market = entry->text(u"type"_s);
                job.revenue = entry->number(u"reward"_s);
                job.experience = static_cast<int>(entry->number(u"exp_delta"_s));
                job.drivenKilometres = static_cast<double>(entry->number(u"travelled_distance"_s));
                job.passengers = static_cast<int>(entry->number(u"total_passengers"_s));
                job.stops = static_cast<int>(entry->number(u"stage_count"_s));
                job.satisfaction = entry->values.value(u"satisfaction"_s).toDouble();
                jobs.append(job);
            }
        }

        // Two logs, each in its own order; together they are ordered by when the job ended.
        std::ranges::stable_sort(jobs, {}, &Statistics::JobRecord::gameTime);
        return jobs;
    }

    QList<Statistics::JobRecord> newDeliveries(const QList<Statistics::JobRecord>& deliveries,
                                               const QList<Statistics::JobRecord>& known)
    {
        // How far apart in game minutes two records of one delivery may be. Two deliveries of the same cargo
        // between the same companies within an in-game hour do not happen.
        constexpr qint64 Tolerance = 60;
        QList<Statistics::JobRecord> result;

        for (const Statistics::JobRecord& delivery : deliveries)
        {
            const bool seen = std::ranges::any_of(known,
                                                  [&delivery](const Statistics::JobRecord& job)
                                                  {
                                                      return job.game == delivery.game &&
                                                          job.sourceCompanyId == delivery.sourceCompanyId &&
                                                          job.destinationCompanyId == delivery.destinationCompanyId &&
                                                          job.cargoId == delivery.cargoId &&
                                                          std::abs(static_cast<qint64>(job.gameTime) -
                                                                   static_cast<qint64>(delivery.gameTime)) <= Tolerance;
                                                  });

            if (!seen)
            {
                result.append(delivery);
            }
        }

        return result;
    }
} // namespace Wheelstar::SaveGame
