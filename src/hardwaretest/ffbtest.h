// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hardwaretest/testanalysis.h"

#include <QObject>
#include <QPointer>

#include <atomic>
#include <mutex>
#include <stop_token>
#include <thread>

namespace Wheelstar::Device
{
    class WheelDevice;
} // namespace Wheelstar::Device

namespace Wheelstar::HardwareTest
{
    /// Drives the wheel through upstream's three hardware tests, each on its own thread because each one is a
    /// sequence of timed force pulses. The wheel positions arrive through steeringSample() from the GUI thread,
    /// which reads the event node.
    class FfbTest : public QObject
    {
        Q_OBJECT

    public:
        enum class Stage
        {
            /// The user holds the wheel and presses a button when the rising force is felt.
            MinimumForce,
            /// Force steps from zero to full, alternating direction.
            Linearity,
            /// Full force right, left, right, then coasting.
            Step,
        };

        explicit FfbTest(Device::WheelDevice* device, QObject* parent = nullptr);
        ~FfbTest() override;

        FfbTest(const FfbTest&) = delete ("owns the test thread, which points back at it");
        FfbTest& operator=(const FfbTest&) = delete ("owns the test thread, which points back at it");
        FfbTest(FfbTest&&) = delete ("owns the test thread, which points back at it");
        FfbTest& operator=(FfbTest&&) = delete ("owns the test thread, which points back at it");

        void run(Stage stage);
        [[nodiscard]] bool isRunning() const;

        /// The range the tests run at, which turns positions into speeds.
        [[nodiscard]] int wheelRange() const;

        void buttonPressed();
        void steeringSample(double position, qint64 timestampUs);

        /// Read only after finished().
        [[nodiscard]] const TestAnalysis::TestData& data() const;

    Q_SIGNALS:
        /// The minimum force test waits for a button press before it starts.
        void awaitingButton();
        /// The minimum force test has started raising the force.
        void forceRising();
        void finished(bool succeeded, const QString& error);

    private:
        struct SavedSettings
        {
            std::optional<int> range;
            std::optional<int> gain;
            std::optional<int> autocenter;
        };

        SavedSettings prepare();
        void restore(const SavedSettings& saved);
        bool centerWheel(const std::stop_token& token);
        bool seedPosition(const std::stop_token& token);
        bool startCollecting(const std::stop_token& token, TestAnalysis::Series* input);
        void stopCollecting(TestAnalysis::Series* input);
        [[nodiscard]] double elapsed() const;

        void runMinimumForce(const std::stop_token& token);
        void runLinearity(const std::stop_token& token);
        void runStep(const std::stop_token& token);
        void finish(bool succeeded, const QString& error = {});

        Device::WheelDevice* m_device;
        TestAnalysis::TestData m_data;
        std::atomic<bool> m_running = false;
        std::atomic<bool> m_awaitingButton = false;
        std::atomic<bool> m_buttonPressed = false;
        std::atomic<bool> m_collecting = false;
        std::atomic<double> m_startTime = 0;
        std::mutex m_outputMutex;
        TestAnalysis::Series* m_output = nullptr;
        std::jthread m_thread;
    };
} // namespace Wheelstar::HardwareTest
