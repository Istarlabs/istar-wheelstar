// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardwaretest/testanalysis.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Wheelstar::HardwareTest::TestAnalysis;
using namespace Qt::StringLiterals;

class TestAnalysisTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resampleHoldsTheLastValue()
    {
        const Series resampled = resample({{0, 1}, {0.003, 2}, {0.005, 3}});

        // The first point's value until 3 ms, the second's until 5 ms; the last value is never emitted.
        const Series expected{{0, 1}, {0.001, 1}, {0.002, 1}, {0.003, 2}, {0.004, 2}};
        QCOMPARE(resampled, expected);
    }

    void periodsMarkEveryChange()
    {
        const Series result = periods({{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, -1}, {5, -1}});
        const Series expected{{0, 0}, {2, 1}, {4, -1}, {5, -1}};
        QCOMPARE(result, expected);
    }

    void filterRepeatsTheEdges()
    {
        // scipy.ndimage.uniform_filter1d([0, 0, 3, 0, 0], 3, mode='nearest') == [0, 1, 1, 1, 0]
        const Series result = filter({{0, 0}, {1, 0}, {2, 3}, {3, 0}, {4, 0}}, 3);
        QCOMPARE(result.at(0).y(), 0.0);
        QCOMPARE(result.at(1).y(), 1.0);
        QCOMPARE(result.at(2).y(), 1.0);
        QCOMPARE(result.at(3).y(), 1.0);
        QCOMPARE(result.at(4).y(), 0.0);

        // An even size leans to the left: [1, 2, 3, 4] with 2 -> [1, 1.5, 2.5, 3.5]
        const Series even = filter({{0, 1}, {1, 2}, {2, 3}, {3, 4}}, 2);
        QCOMPARE(even.at(1).y(), 1.5);
        QCOMPARE(even.at(3).y(), 3.5);
    }

    void deriveIsTheSlope()
    {
        const Series result = derive({{0, 0}, {0.5, 1}, {1, 1}}, 2);
        const Series expected{{0, 0}, {0.5, 4}, {1, 0}};
        QCOMPARE(result, expected);
    }

    void analysesAStepResponse()
    {
        // A wheel that starts turning 20 ms after the force, at a steady acceleration.
        const auto data = syntheticData();
        const auto report = analyse(data, 900);
        QVERIFY2(report.has_value(), report.has_value() ? "" : qPrintable(report.error()));
        QVERIFY(std::abs(report->performance.latency - 0.02) < 0.01);
        QVERIFY(report->performance.maxVelocity > 0);
        QVERIFY(report->performance.meanAcceleration > 0);
        QCOMPARE(report->linear.minimumLevelPercent, 300 * 100 / double(0x7fff));
    }

    void failsWithoutMovement()
    {
        TestData data = syntheticData();

        for (QPointF& point : data.performanceOutput)
        {
            point.setY(0);
        }

        QVERIFY(!analyse(data, 900).has_value());
    }

    void csvRoundTrip()
    {
        const auto report = analyse(syntheticData(), 900);
        QVERIFY(report.has_value());

        const QTemporaryDir directory;
        const QString path = directory.filePath(u"report.csv"_s);
        QVERIFY(exportCsv(path, *report).has_value());

        const auto imported = importCsv(path);
        QVERIFY2(imported.has_value(), imported.has_value() ? "" : qPrintable(imported.error()));
        QCOMPARE(imported->minimumLevel, 300.0);

        const auto again = analyse(*imported, 900);
        QVERIFY(again.has_value());
        QVERIFY(std::abs(again->performance.latency - report->performance.latency) < 0.002);
    }

private:
    static TestData syntheticData()
    {
        TestData data;
        data.minimumLevel = 300;

        // Linearity: force steps of 0.3 s, the wheel moving a little faster with each.
        data.linearInput = {{0, 0}};
        data.linearOutput = {{0, 0}};
        double position = 0;

        for (int step = 1; step <= 10; ++step)
        {
            const double start = step * 0.3;
            const double force = (step % 2 == 0 ? -1 : 1) * step / 10.0;
            data.linearInput.append({start, force});

            for (int sample = 0; sample < 30; ++sample)
            {
                position += force * 0.001;
                data.linearOutput.append({start + (sample * 0.01), position});
            }
        }

        data.linearInput.append({3.3, 0});

        // Step: right at 0.1 s, left at 0.4 s, right at 0.7 s, coast from 1.0 s, end at 1.5 s.
        data.performanceInput = {{0, 0}, {0.1, 1}, {0.4, -1}, {0.7, 1}, {1.0, 0}, {1.5, 0}};
        double velocity = 0;
        position = 0;

        for (int millisecond = 0; millisecond <= 1500; ++millisecond)
        {
            const double time = millisecond / 1000.0;
            // The force the wheel feels: from 20 ms after each step's start (the latency) until the next.
            double force = 0;

            if (time >= 0.12 && time < 0.4)
            {
                force = 1;
            }
            else if (time >= 0.4 && time < 0.7)
            {
                force = -1;
            }
            else if (time >= 0.7 && time < 1.0)
            {
                force = 1;
            }
            velocity += (force * 0.01) - (velocity * 0.001);
            position += velocity * 0.001;
            data.performanceOutput.append({time, position});
        }

        return data;
    }
};

QTEST_GUILESS_MAIN(TestAnalysisTest)

#include "testanalysistest.moc"
