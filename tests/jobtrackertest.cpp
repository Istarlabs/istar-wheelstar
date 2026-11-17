// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/jobtracker.h"

#include <QTest>

#include <cstring>

using namespace Wheelstar::Statistics;
using namespace Wheelstar::Telemetry;

namespace
{
    void setText(EventText& field, const char* text)
    {
        field.fill(0);
        std::memcpy(field.data(), text, std::strlen(text));
    }

    Event taken(const char* cargoId)
    {
        Event event;
        event.game = Game::EuroTruckSimulator2;
        event.kind = EventKind::JobTaken;
        event.amount = 24000;
        event.plannedKilometres = 812;
        setText(event.sourceCity, "Hamburg");
        setText(event.sourceCityId, "hamburg");
        setText(event.sourceCompanyId, "posped");
        setText(event.destinationCity, "München");
        setText(event.destinationCityId, "munchen");
        setText(event.destinationCompanyId, "tradeaux");
        setText(event.cargoId, cargoId);
        return event;
    }

    Event finished(EventKind kind, std::int64_t amount)
    {
        Event event;
        event.game = Game::EuroTruckSimulator2;
        event.kind = kind;
        event.amount = amount;
        event.gameTime = 5000;
        event.kilometres = 820;
        event.experience = 900;
        return event;
    }
} // namespace

class JobTrackerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aDeliveryCompletesTheJobTaken()
    {
        JobTracker tracker;
        const QDateTime start(QDate(2026, 11, 15), QTime(16, 0), QTimeZone::UTC);

        QVERIFY(std::holds_alternative<std::monostate>(tracker.add(taken("gravel"), start)));
        // The game announces the open job again when it starts; the job keeps the time it was really taken.
        QVERIFY(std::holds_alternative<std::monostate>(tracker.add(taken("gravel"), start.addSecs(600))));

        const auto record = tracker.add(finished(EventKind::JobDelivered, 25100), start.addSecs(3600));
        const auto* job = std::get_if<JobRecord>(&record);

        QVERIFY(job != nullptr);
        QVERIFY(job->delivered);
        QCOMPARE(job->takenAt, start);
        QCOMPARE(job->sourceCity, QStringLiteral("Hamburg"));
        QCOMPARE(job->destinationCity, QStringLiteral("München"));
        QCOMPARE(job->cargoId, QStringLiteral("gravel"));
        QCOMPARE(job->revenue, 25100);
        QCOMPARE(job->drivenKilometres, 820.0);
        QCOMPARE(job->plannedKilometres, 812.0);
        QCOMPARE(job->gameTime, 5000U);
    }

    void aCancellationIsANegativeRevenue()
    {
        JobTracker tracker;
        tracker.add(taken("gravel"), QDateTime::currentDateTimeUtc());

        const auto record = tracker.add(finished(EventKind::JobCancelled, 3000), QDateTime::currentDateTimeUtc());

        QVERIFY(!std::get<JobRecord>(record).delivered);
        QCOMPARE(std::get<JobRecord>(record).revenue, -3000);
        QCOMPARE(std::get<JobRecord>(record).drivenKilometres, 0.0);
    }

    void aFineIsAnEvent()
    {
        JobTracker tracker;
        Event fine = finished(EventKind::Fined, 750);
        setText(fine.detail, "red_signal");

        const auto record = tracker.add(fine, QDateTime::currentDateTimeUtc());
        const auto* event = std::get_if<GameEventRecord>(&record);

        QVERIFY(event != nullptr);
        QCOMPARE(event->kind, GameEventRecord::Kind::Fine);
        QCOMPARE(event->detail, QStringLiteral("red_signal"));
        QCOMPARE(event->amount, 750);
    }

    void recordsSurviveJson()
    {
        JobTracker tracker;
        tracker.add(taken("gravel"), QDateTime(QDate(2026, 11, 15), QTime(16, 0), QTimeZone::UTC));
        const JobRecord job = std::get<JobRecord>(tracker.add(
            finished(EventKind::JobDelivered, 25100), QDateTime(QDate(2026, 11, 15), QTime(17, 0), QTimeZone::UTC)));

        const auto restored = JobRecord::fromJson(job.toJson());

        QVERIFY(restored.has_value());
        QCOMPARE(restored->takenAt, job.takenAt);
        QCOMPARE(restored->destinationCityId, job.destinationCityId);
        QCOMPARE(restored->revenue, job.revenue);
        QVERIFY(!GameEventRecord::fromJson(job.toJson()).has_value());
    }
};

QTEST_GUILESS_MAIN(JobTrackerTest)
#include "jobtrackertest.moc"
