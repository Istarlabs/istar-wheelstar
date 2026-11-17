// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hardwaretest/ffbtest.h"
#include "device/wheeldevice.h"

#include <KLocalizedString>

#include <chrono>
#include <cmath>
#include <condition_variable>

using namespace std::chrono_literals;

namespace Wheelstar::HardwareTest
{
    namespace
    {
        /// The direction of a constant force as the input layer counts it: a 16 bit angle where the full circle is
        /// 0x10000, so 0x4000 (a quarter) pulls to the left and 0xc000 (three quarters) to the right.
        constexpr std::uint16_t Left = 0x4000;
        constexpr std::uint16_t Right = 0xc000;
        /// The largest level of a force effect, 32767 (see TestAnalysis's MaxLevel).
        constexpr int FullForce = 0x7fff;

        /// Sleeps, but wakes at once when the test is cancelled. False when it was.
        bool pause(const std::stop_token& token, std::chrono::milliseconds duration)
        {
            std::mutex mutex;
            std::condition_variable_any condition;
            std::unique_lock lock(mutex);
            condition.wait_for(lock, token, duration, [] { return false; });
            return !token.stop_requested();
        }

        double seconds(std::chrono::steady_clock::duration duration)
        {
            return std::chrono::duration<double>(duration).count();
        }
    } // namespace

    FfbTest::FfbTest(Device::WheelDevice* device, QObject* parent) : QObject(parent), m_device(device)
    {
    }

    FfbTest::~FfbTest()
    {
        // jthread asks the thread to stop and joins it; the pauses wake on the request.
        m_thread = {};
    }

    void FfbTest::run(Stage stage)
    {
        m_thread = {};
        m_running = true;
        m_buttonPressed = false;

        switch (stage)
        {
            case Stage::MinimumForce:
                m_thread = std::jthread([this](const std::stop_token& token) { runMinimumForce(token); });
                break;
            case Stage::Linearity:
                m_thread = std::jthread([this](const std::stop_token& token) { runLinearity(token); });
                break;
            case Stage::Step:
                m_thread = std::jthread([this](const std::stop_token& token) { runStep(token); });
                break;
        }
    }

    bool FfbTest::isRunning() const
    {
        return m_running;
    }

    int FfbTest::wheelRange() const
    {
        // The test runs at 900 degrees at most, or at the wheel's own limit if that is smaller; the analysis turns
        // positions into rpm with this number, so it has to be the range the wheel was set to.
        return std::min(900, m_device->maxRange());
    }

    void FfbTest::buttonPressed()
    {
        if (m_awaitingButton)
        {
            m_buttonPressed = true;
        }
    }

    void FfbTest::steeringSample(double position, qint64 timestampUs)
    {
        if (!m_collecting)
        {
            return;
        }

        const std::scoped_lock lock(m_outputMutex);

        if (m_output != nullptr)
        {
            // Microseconds to seconds (1e6), minus the start of the measurement, so the series begins at 0.
            m_output->append({(static_cast<double>(timestampUs) / 1e6) - m_startTime, position});
        }
    }

    const TestAnalysis::TestData& FfbTest::data() const
    {
        return m_data;
    }

    double FfbTest::elapsed() const
    {
        return seconds(std::chrono::steady_clock::now().time_since_epoch()) - m_startTime;
    }

    FfbTest::SavedSettings FfbTest::prepare()
    {
        SavedSettings saved{
            .range = m_device->attribute("range"), .gain = m_device->ffGain(), .autocenter = m_device->autocenter()};

        m_device->setAttribute("range", wheelRange());
        m_device->setFfGain(100);
        m_device->setAutocenter(0);
        return saved;
    }

    void FfbTest::restore(const SavedSettings& saved)
    {
        // Upstream restored these only after the first test, leaving the wheel at 900° and full gain after the others.
        if (saved.range)
        {
            m_device->setAttribute("range", *saved.range);
        }

        if (saved.gain)
        {
            m_device->setFfGain(*saved.gain);
        }

        if (saved.autocenter)
        {
            m_device->setAutocenter(*saved.autocenter);
        }
    }

    bool FfbTest::centerWheel(const std::stop_token& token)
    {
        m_device->setAutocenter(100);
        const bool completed = pause(token, 1s);
        m_device->setAutocenter(0);

        // Let the wheel settle and the last events arrive.
        return completed && pause(token, 500ms);
    }

    bool FfbTest::seedPosition(const std::stop_token& token)
    {
        // A small nudge (0x2000 is a quarter of the strongest force, for 0.1 s), so the wheel reports a position
        // before the measurement starts.
        const int effect = m_device->uploadConstantEffect(-1, Left, 0x2000);

        if (effect < 0)
        {
            return false;
        }

        m_device->playEffect(effect, true);
        const bool completed = pause(token, 100ms);
        m_device->playEffect(effect, false);
        m_device->eraseEffect(effect);
        return completed;
    }

    bool FfbTest::startCollecting(const std::stop_token& token, TestAnalysis::Series* input)
    {
        {
            const std::scoped_lock lock(m_outputMutex);
            m_output->clear();
            m_output->append({0, m_device->steering()});
        }

        input->clear();
        input->append({0, 0});
        m_startTime = seconds(std::chrono::steady_clock::now().time_since_epoch());

        if (!pause(token, 100ms))
        {
            return false;
        }

        m_collecting = true;
        return true;
    }

    void FfbTest::stopCollecting(TestAnalysis::Series* input)
    {
        input->append({elapsed(), 0});
        m_collecting = false;
    }

    void FfbTest::runMinimumForce(const std::stop_token& token)
    {
        const SavedSettings saved = prepare();

        if (!centerWheel(token))
        {
            restore(saved);
            finish(false);
            return;
        }

        m_awaitingButton = true;
        QMetaObject::invokeMethod(this, &FfbTest::awaitingButton, Qt::QueuedConnection);

        while (!m_buttonPressed)
        {
            if (!pause(token, 50ms))
            {
                m_awaitingButton = false;
                restore(saved);
                finish(false);
                return;
            }
        }

        m_buttonPressed = false;
        QMetaObject::invokeMethod(this, &FfbTest::forceRising, Qt::QueuedConnection);

        int left = m_device->uploadConstantEffect(-1, Left, 0);
        int right = m_device->uploadConstantEffect(-1, Right, 0);
        bool succeeded = left >= 0 && right >= 0;

        if (succeeded)
        {
            m_device->playEffect(left, true);
            m_device->playEffect(right, true);
            m_data.minimumLevel = 0;

            // Short pulses each way at a slowly rising level, until the user feels them. A step of 30 is under a
            // thousandth of the range (30 of 32767), fine enough to find the weakest force that is felt; at 0.2 s
            // for a pair of pulses the ramp to full force takes about three and a half minutes at most.
            for (int level = 0; level < FullForce && succeeded; level += 30)
            {
                const auto force = static_cast<std::int16_t>(level);
                left = m_device->uploadConstantEffect(left, Left, force);
                succeeded = pause(token, 100ms);
                m_device->uploadConstantEffect(left, Left, 0);
                right = m_device->uploadConstantEffect(right, Right, force);
                succeeded = succeeded && pause(token, 100ms);
                m_device->uploadConstantEffect(right, Right, 0);

                if (m_buttonPressed)
                {
                    m_data.minimumLevel = level;
                    break;
                }
            }
        }

        m_awaitingButton = false;
        m_device->eraseEffect(left);
        m_device->eraseEffect(right);
        restore(saved);
        finish(succeeded, succeeded ? QString() : i18n("The force feedback effects could not be played."));
    }

    void FfbTest::runLinearity(const std::stop_token& token)
    {
        const SavedSettings saved = prepare();
        m_output = &m_data.linearOutput;

        const int left = m_device->uploadConstantEffect(-1, Left, 0);
        const int right = m_device->uploadConstantEffect(-1, Right, 0);
        bool succeeded = left >= 0 && right >= 0 && seedPosition(token) && centerWheel(token) &&
            startCollecting(token, &m_data.linearInput);

        // 51 steps from no force to full force, each 0.3 s, alternating right and left.
        for (int step = 0; step <= 50 && succeeded; ++step)
        {
            // Step 0 is no force, step 50 the full force, evenly between: step / 50 of FullForce, rounded.
            const auto level = static_cast<std::int16_t>(std::lround(step * FullForce / 50.0));
            const bool toRight = step % 2 == 0;
            const int effect = toRight ? right : left;

            m_device->uploadConstantEffect(effect, toRight ? Right : Left, level);
            m_device->playEffect(effect, true);
            // The input as a share of the full force, signed: positive to the right, negative to the left.
            m_data.linearInput.append({elapsed(), (toRight ? 1.0 : -1.0) * level / FullForce});
            succeeded = pause(token, 300ms);
            m_device->playEffect(effect, false);
        }

        stopCollecting(&m_data.linearInput);
        m_device->eraseEffect(left);
        m_device->eraseEffect(right);
        restore(saved);
        finish(succeeded,
               succeeded || token.stop_requested() ? QString()
                                                   : i18n("The force feedback effects could not be played."));
    }

    void FfbTest::runStep(const std::stop_token& token)
    {
        const SavedSettings saved = prepare();
        m_output = &m_data.performanceOutput;

        const int left = m_device->uploadConstantEffect(-1, Left, FullForce);
        const int right = m_device->uploadConstantEffect(-1, Right, FullForce);
        bool succeeded = left >= 0 && right >= 0 && seedPosition(token) && centerWheel(token) &&
            startCollecting(token, &m_data.performanceInput);

        // Full force right, left and right again, then let the wheel coast.
        for (const auto& [effect, direction] : {std::pair{right, 1.0}, {left, -1.0}, {right, 1.0}})
        {
            if (!succeeded)
            {
                break;
            }

            m_data.performanceInput.append({elapsed(), direction});
            m_device->playEffect(effect, true);
            succeeded = pause(token, 300ms);
            m_device->playEffect(effect, false);
        }

        if (succeeded)
        {
            m_data.performanceInput.append({elapsed(), 0});
            succeeded = pause(token, 500ms);
        }

        stopCollecting(&m_data.performanceInput);
        m_device->eraseEffect(left);
        m_device->eraseEffect(right);
        restore(saved);
        finish(succeeded,
               succeeded || token.stop_requested() ? QString()
                                                   : i18n("The force feedback effects could not be played."));
    }

    void FfbTest::finish(bool succeeded, const QString& error)
    {
        m_running = false;
        QMetaObject::invokeMethod(
            this, [this, succeeded, error] { Q_EMIT finished(succeeded, error); }, Qt::QueuedConnection);
    }
} // namespace Wheelstar::HardwareTest
