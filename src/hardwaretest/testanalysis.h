// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QPointF>
#include <QString>

#include <expected>
#include <optional>

/// The signal processing behind the wheel tests, ported from upstream's signal.py, linear_chart.py and
/// performance_chart.py. A series is (time in seconds, value) pairs.
namespace Wheelstar::HardwareTest::TestAnalysis
{
    using Series = QList<QPointF>;

    /// What the three test runs measured, or what a CSV report holds.
    struct TestData
    {
        /// The force level (0 to 0x7fff) at which the user first felt the wheel move.
        double minimumLevel = 0;
        Series linearInput;
        Series linearOutput;
        Series performanceInput;
        Series performanceOutput;
    };

    struct LinearResult
    {
        Series input;
        Series output;
        Series fixedInput;
        Series linearity;
        double minimumLevelPercent = 0;
    };

    struct PerformanceResult
    {
        Series input;
        Series position;
        Series filteredPosition;
        Series velocity;
        Series filteredVelocity;
        Series acceleration;
        Series filteredAcceleration;
        double latency = 0;
        double maxVelocity = 0;
        double maxAcceleration = 0;
        double timeToMaxAcceleration = 0;
        double maxDeceleration = 0;
        double timeToMaxDeceleration = 0;
        double meanAcceleration = 0;
        double meanDeceleration = 0;
        double residualDeceleration = 0;
        double estimatedSnr = 0;
    };

    struct Report
    {
        TestData data;
        LinearResult linear;
        PerformanceResult performance;
    };

    /// One sample per millisecond, each holding the value last seen.
    Series resample(const Series& data);
    /// The points where the value changes, plus the first and the last.
    Series periods(const Series& data);
    Series derive(const Series& data, double multiplier = 1.0);
    /// A moving average over `size` samples; the edges repeat the outermost value (scipy's uniform_filter1d).
    Series filter(const Series& data, int size);
    Series slice(const Series& data, double from, double to);
    std::optional<double> zeroCrossing(const Series& data, double from, double to);

    /// `wheelRange` is the rotation range in degrees the test ran at; it turns positions into RPM. Fails when
    /// the wheel did not move.
    std::expected<Report, QString> analyse(const TestData& data, int wheelRange);

    std::expected<TestData, QString> importCsv(const QString& path);
    std::expected<void, QString> exportCsv(const QString& path, const Report& report);
} // namespace Wheelstar::HardwareTest::TestAnalysis
