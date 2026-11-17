// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardwaretest/testanalysis.h"

#include <KLocalizedString>

#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <ranges>

using namespace Qt::StringLiterals;

namespace Wheelstar::HardwareTest::TestAnalysis
{
    namespace
    {
        /// The strongest force level of a Linux force feedback effect: 0x7fff is 32767, the largest signed 16 bit
        /// number. A level divided by it is the share of the full force.
        constexpr double MaxLevel = 0x7fff;

        double maxAbsolute(const Series& data)
        {
            double result = 0;

            for (const QPointF& point : data)
            {
                result = std::max(result, std::abs(point.y()));
            }

            return result;
        }

        /// The point with the largest absolute value, or none for an empty series.
        std::optional<QPointF> peak(const Series& data)
        {
            if (data.isEmpty())
            {
                return std::nullopt;
            }

            return *std::ranges::max_element(data, {}, [](const QPointF& point) { return std::abs(point.y()); });
        }

        /// How far the position wanders while the wheel should be at rest: the largest distance from the mean, up or
        /// down. A later movement only counts as one when it exceeds this.
        double noiseLevel(const Series& data, double from, double to)
        {
            const Series part = slice(data, from, to);

            if (part.isEmpty())
            {
                return 0;
            }

            double sum = 0;
            double minimum = part.first().y();
            double maximum = minimum;

            for (const QPointF& point : part)
            {
                sum += point.y();
                minimum = std::min(minimum, point.y());
                maximum = std::max(maximum, point.y());
            }

            const double mean = sum / static_cast<double>(part.size());
            return std::max(mean - minimum, maximum - mean);
        }

        /// A signal-to-noise ratio in decibels, taking the smoothed position as the signal and its difference to the
        /// raw one as the noise. Both are reduced to their root mean square (the square root of the mean of the
        /// squares, a size that ignores the sign), and the decibel is ten times the log10 of their ratio. That is the
        /// formula of the Python Oversteer, kept so the numbers stay comparable; a strict amplitude ratio would take
        /// twenty times the log.
        double estimatedSnr(const Series& raw, const Series& filtered)
        {
            double noise = 0;
            double signal = 0;
            const auto count = std::min(raw.size(), filtered.size());

            // zip stops at the shorter of the two, which is what `count` says as well.
            for (const auto& [rawPoint, filteredPoint] : std::views::zip(raw, filtered))
            {
                noise += std::pow(filteredPoint.y() - rawPoint.y(), 2);
                signal += std::pow(filteredPoint.y(), 2);
            }

            // No noise at all would divide by zero. The fixed 30 dB stands for "clean" and is a convention, not a
            // measurement.
            if (noise == 0 || count == 0)
            {
                return 30;
            }

            return 10 *
                std::log10(std::sqrt(signal / static_cast<double>(count)) /
                           std::sqrt(noise / static_cast<double>(count)));
        }

        /// RPM per unit of normalised position per second: the position runs from -1 to 1 over the range.
        ///
        /// A change of 1 in the position is half the range in degrees (wheelRange / 2); times 60 that is degrees per
        /// minute, divided by 360 degrees to a turn it is turns per minute. So at 900 degrees one position unit per
        /// second is 450 * 60 / 360 = 75 rpm.
        double rpmFactor(int wheelRange)
        {
            return wheelRange / 2.0 * 60.0 / 360.0;
        }

        std::expected<LinearResult, QString> analyseLinear(const TestData& data, int wheelRange)
        {
            LinearResult result;
            result.input = resample(data.linearInput);
            result.output = resample(data.linearOutput);
            // The level at which the wheel was first felt, as a percentage of the strongest force.
            result.minimumLevelPercent = data.minimumLevel * 100 / MaxLevel;

            const Series inputPeriods = periods(result.input);

            if (inputPeriods.size() < 3)
            {
                return std::unexpected(i18n("The linearity test data is incomplete."));
            }

            // The position is smoothed over 5 samples (5 ms, the series has one per millisecond), turned into a
            // speed in rpm, and smoothed again over 10. The window sizes are taken over from the Python Oversteer, not
            // derived: a derivative amplifies noise, so some smoothing is needed on both sides of it.
            const Series filteredVelocity = filter(derive(filter(result.output, 5), rpmFactor(wheelRange)), 10);

            for (const QPointF& point : result.input)
            {
                result.fixedInput.append({point.x(), std::abs(point.y())});
            }

            // The fastest the wheel turned during each force step, scaled to the input's peak.
            Series linearity{{0, 0}};
            double previous = inputPeriods.at(1).x();

            for (const QPointF& period : inputPeriods | std::views::drop(2))
            {
                linearity.append({period.x(), maxAbsolute(slice(filteredVelocity, previous, period.x()))});
                previous = period.x();
            }

            // The speeds are in rpm and the input in force shares from 0 to 1; one factor makes the fastest speed
            // as tall as the strongest input, so a perfectly linear wheel draws the same line as the input.
            const double scale = maxAbsolute(linearity) > 0 ? maxAbsolute(result.input) / maxAbsolute(linearity) : 0;

            for (QPointF& point : linearity)
            {
                point.setY(point.y() * scale);
            }

            result.linearity = linearity;
            return result;
        }

        std::expected<PerformanceResult, QString> analysePerformance(const TestData& data, int wheelRange)
        {
            PerformanceResult result;
            result.input = resample(data.performanceInput);
            result.position = resample(data.performanceOutput);
            result.filteredPosition = filter(result.position, 20);
            result.velocity = derive(result.filteredPosition, rpmFactor(wheelRange));
            result.filteredVelocity = filter(result.velocity, 15);
            result.acceleration = derive(result.filteredVelocity);
            result.filteredAcceleration = filter(result.acceleration, 15);

            // Rest, right, left, right, coast, end.
            const Series steps = periods(result.input);

            if (steps.size() < 6)
            {
                return std::unexpected(i18n("The step test data is incomplete."));
            }

            const auto start = [&](qsizetype step) { return steps.at(step).x(); };

            const double noise = noiseLevel(result.position, start(0), start(1));
            const Series moving = slice(result.position, start(1), start(2));
            const auto firstMove = std::ranges::find_if(moving, [&](const QPointF& point)
                                                        { return std::abs(point.y() - moving.first().y()) > noise; });

            if (moving.isEmpty() || firstMove == moving.end())
            {
                return std::unexpected(i18n("No wheel movement could be registered."));
            }

            result.latency = firstMove->x() - start(1);

            const Series accelerating = slice(result.filteredVelocity, start(1), start(2));
            result.maxVelocity = maxAbsolute(accelerating);

            const Series acceleration = slice(result.filteredAcceleration, start(1), start(2));
            result.maxAcceleration = maxAbsolute(acceleration);
            result.timeToMaxAcceleration = peak(acceleration).value_or(QPointF(start(1), 0)).x() - start(1);

            // Upstream divided by the time since the test began; the acceleration phase begins with the movement.
            if (const auto top = std::ranges::find_if(accelerating, [&](const QPointF& point)
                                                      { return std::abs(point.y()) >= result.maxVelocity; });
                top != accelerating.end() && top->x() - start(1) - result.latency > 0)
            {
                result.meanAcceleration = std::abs(top->y()) / (top->x() - start(1) - result.latency);
            }

            const double brakingStart = start(2);
            if (const double stopped = zeroCrossing(result.filteredVelocity, start(2), start(3)).value_or(start(3));
                brakingStart < stopped)
            {
                const Series braking = slice(result.filteredAcceleration, brakingStart, stopped);
                result.maxDeceleration = maxAbsolute(braking);
                result.timeToMaxDeceleration = peak(braking).value_or(QPointF(brakingStart, 0)).x() - brakingStart;
                // Average deceleration: the speed it had to lose (its top speed) over the time it took to stop.
                result.meanDeceleration = result.maxVelocity / (stopped - brakingStart);
            }

            // Only the first 0.2 s of the coasting count (or until the wheel stops, if sooner): the deceleration
            // that is left with no force applied, which is friction. Later the wheel is nearly still and the
            // numbers are noise. The 0.2 s is a chosen window.
            const double coastStart = start(4);
            const double coastEnd = std::min(
                coastStart + 0.2, zeroCrossing(result.filteredVelocity, start(4), start(5)).value_or(start(5)));
            if (const Series coasting = slice(result.filteredAcceleration, coastStart, coastEnd); !coasting.isEmpty())
            {
                double sum = 0;

                for (const QPointF& point : coasting)
                {
                    sum += point.y();
                }

                result.residualDeceleration = std::abs(sum / static_cast<double>(coasting.size()));
            }

            result.estimatedSnr = estimatedSnr(result.position, result.filteredPosition);
            return result;
        }

        QString format(double value)
        {
            return QString::number(value, 'f', 5);
        }
    } // namespace

    /// Turns the events, which come at irregular times, into one value per millisecond: each value holds until the
    /// next event. The filters and derivatives below assume that even spacing.
    Series resample(const Series& data)
    {
        Series result;
        qint64 previousTime = 0;
        double previousValue = 0;

        for (const QPointF& point : data)
        {
            qint64 time = previousTime;

            if (point.x() > 0)
            {
                // Seconds to whole milliseconds, rounded up so an event never lands before it happened.
                time = static_cast<qint64>(std::ceil(point.x() * 1000));

                for (qint64 millisecond = previousTime; millisecond < time; ++millisecond)
                {
                    result.append({static_cast<double>(millisecond) / 1000, previousValue});
                }
            }

            previousTime = std::max(time, previousTime);
            previousValue = point.y();
        }

        return result;
    }

    Series periods(const Series& data)
    {
        if (data.isEmpty())
        {
            return {};
        }

        Series result{data.first()};
        QPointF last = data.first();

        for (const QPointF& point : data)
        {
            if (point.y() != last.y())
            {
                result.append(point);
                last = point;
            }
        }

        if (data.last().x() != last.x())
        {
            result.append(data.last());
        }

        return result;
    }

    /// The slope between neighbouring points: change in y over change in x, times `multiplier` for a unit change.
    Series derive(const Series& data, double multiplier)
    {
        Series result{{0, 0}};

        for (const auto& [previous, current] : data | std::views::adjacent<2>)
        {
            if (current.x() != previous.x())
            {
                result.append({current.x(), (current.y() - previous.y()) * multiplier / (current.x() - previous.x())});
            }
        }

        return result;
    }

    /// A moving average over `size` points, centred on each point: the mean of the point and its neighbours. Near
    /// the ends the missing neighbours are replaced by the first or last point.
    Series filter(const Series& data, int size)
    {
        const qsizetype count = data.size();
        Series result;
        result.reserve(count);

        for (qsizetype index = 0; index < count; ++index)
        {
            double sum = 0;

            for (qsizetype offset = 0; offset < size; ++offset)
            {
                sum += data.at(std::clamp<qsizetype>(index - (size / 2) + offset, 0, count - 1)).y();
            }

            result.append({data.at(index).x(), sum / size});
        }

        return result;
    }

    Series slice(const Series& data, double from, double to)
    {
        Series result;

        for (const QPointF& point : data)
        {
            if (from <= point.x() && point.x() < to)
            {
                result.append(point);
            }
        }

        return result;
    }

    std::optional<double> zeroCrossing(const Series& data, double from, double to)
    {
        double previous = 0;

        for (const QPointF& point : slice(data, from, to))
        {
            if (point.y() == 0 || point.y() * previous < 0)
            {
                return point.x();
            }

            previous = point.y();
        }

        return std::nullopt;
    }

    std::expected<Report, QString> analyse(const TestData& data, int wheelRange)
    {
        auto linear = analyseLinear(data, wheelRange);

        if (!linear)
        {
            return std::unexpected(linear.error());
        }

        auto performance = analysePerformance(data, wheelRange);

        if (!performance)
        {
            return std::unexpected(performance.error());
        }

        return Report{.data = data, .linear = std::move(*linear), .performance = std::move(*performance)};
    }

    std::expected<TestData, QString> importCsv(const QString& path)
    {
        QFile file(path);

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return std::unexpected(i18n("Cannot read \"%1\": %2", path, file.errorString()));
        }

        TestData data;
        bool performanceBlock = false;

        for (const QString& line : QString::fromUtf8(file.readAll()).split(u'\n', Qt::SkipEmptyParts))
        {
            const QStringList row = line.trimmed().split(u',');

            if (row.first().startsWith(u'#'))
            {
                continue;
            }

            if (row.first() == "minimum_level"_L1 && row.size() > 1)
            {
                data.minimumLevel = row.at(1).toDouble();
            }
            else if (row.first() == "linear_data"_L1 || row.first() == "performance_data"_L1)
            {
                performanceBlock = row.first() == "performance_data"_L1;
            }
            else if (row.size() >= 4)
            {
                Series& input = performanceBlock ? data.performanceInput : data.linearInput;
                Series& output = performanceBlock ? data.performanceOutput : data.linearOutput;
                input.append({row.at(0).toDouble(), row.at(1).toDouble()});
                output.append({row.at(2).toDouble(), row.at(3).toDouble()});
            }
            else
            {
                return std::unexpected(i18n("\"%1\" is not a test report.", path));
            }
        }

        return data;
    }

    std::expected<void, QString> exportCsv(const QString& path, const Report& report)
    {
        QSaveFile file(path);

        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            return std::unexpected(i18n("Cannot write \"%1\": %2", path, file.errorString()));
        }

        QString text = u"minimum_level,%1\nlinear_data\n"_s.arg(report.data.minimumLevel);

        const auto writeRows = [&](const Series& input, const Series& output)
        {
            for (const auto& [in, out] : std::views::zip(input, output))
            {
                text += u"%1,%2,%3,%4\n"_s.arg(format(in.x()), format(in.y()), format(out.x()), format(out.y()));
            }
        };

        writeRows(report.linear.input, report.linear.output);
        text += "performance_data\n"_L1;
        writeRows(report.performance.input, report.performance.position);

        file.write(text.toUtf8());

        if (!file.commit())
        {
            return std::unexpected(i18n("Cannot write \"%1\": %2", path, file.errorString()));
        }

        return {};
    }
} // namespace Wheelstar::HardwareTest::TestAnalysis
