// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/jobtracker.h"
#include "telemetry/telemetryreceiver.h"

namespace Wheelstar::Statistics
{
    namespace
    {
        using Telemetry::EventKind;
        using Telemetry::eventText;

        JobRecord jobTaken(const Telemetry::Event& event, const QDateTime& now)
        {
            JobRecord job;
            job.game = event.game;
            job.takenAt = now;
            job.sourceCityId = eventText(event.sourceCityId);
            job.sourceCompanyId = eventText(event.sourceCompanyId);
            job.destinationCityId = eventText(event.destinationCityId);
            job.destinationCompanyId = eventText(event.destinationCompanyId);
            job.cargoId = eventText(event.cargoId);
            job.sourceCity = eventText(event.sourceCity);
            job.sourceCompany = eventText(event.sourceCompany);
            job.destinationCity = eventText(event.destinationCity);
            job.destinationCompany = eventText(event.destinationCompany);
            job.cargo = eventText(event.cargo);
            job.market = eventText(event.detail);
            job.mass = static_cast<double>(event.mass);
            job.plannedKilometres = static_cast<double>(event.plannedKilometres);
            // The offered income, until the delivery says what was paid.
            job.revenue = event.amount;
            job.special = (event.flags & Telemetry::SpecialJob) != 0;
            return job;
        }

        bool sameJob(const JobRecord& a, const JobRecord& b)
        {
            return a.game == b.game && a.sourceCompanyId == b.sourceCompanyId &&
                a.destinationCompanyId == b.destinationCompanyId && a.cargoId == b.cargoId;
        }
    } // namespace

    JobTracker::Record JobTracker::add(const Telemetry::Event& event, const QDateTime& now)
    {
        switch (event.kind)
        {
            case EventKind::JobTaken:
            {
                JobRecord taken = jobTaken(event, now);

                // The plugin announces the current job again whenever the game starts; that is the job already open,
                // and it keeps the time it was really taken.
                if (!m_open || !sameJob(*m_open, taken))
                {
                    m_open = std::move(taken);
                }

                return std::monostate{};
            }
            case EventKind::JobDelivered:
            case EventKind::JobCancelled:
            {
                // A job whose start this program did not see still counts, with what the end tells.
                JobRecord job = m_open.value_or(JobRecord{});
                m_open.reset();
                job.game = event.game;
                job.finishedAt = now;
                job.gameTime = event.gameTime;
                job.delivered = event.kind == EventKind::JobDelivered;
                job.revenue = job.delivered ? event.amount : -event.amount;

                if (job.delivered)
                {
                    job.experience = event.experience;
                    job.cargoDamage = static_cast<double>(event.cargoDamage);
                    job.drivenKilometres = static_cast<double>(event.kilometres);
                    job.deliveryMinutes = event.minutes;
                    job.autoParked = (event.flags & Telemetry::AutoParked) != 0;
                    job.autoLoaded = (event.flags & Telemetry::AutoLoaded) != 0;
                }

                return job;
            }
            case EventKind::Fined:
            case EventKind::TollPaid:
            case EventKind::Ferry:
            case EventKind::Train:
            {
                GameEventRecord record;
                record.game = event.game;
                record.occurredAt = now;
                record.gameTime = event.gameTime;
                record.amount = event.amount;
                record.detail = eventText(event.detail);
                record.from = eventText(event.sourceCity);
                record.to = eventText(event.destinationCity);
                record.kind = event.kind == EventKind::Fined ? GameEventRecord::Kind::Fine
                    : event.kind == EventKind::TollPaid      ? GameEventRecord::Kind::Toll
                    : event.kind == EventKind::Ferry         ? GameEventRecord::Kind::Ferry
                                                             : GameEventRecord::Kind::Train;
                return record;
            }
            case EventKind::Unknown:
                break;
        }

        return std::monostate{};
    }
} // namespace Wheelstar::Statistics
