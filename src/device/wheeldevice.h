// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "device/wheelmodels.h"

#include <QList>
#include <QObject>
#include <QString>

#include <linux/input.h>

#include <array>
#include <atomic>
#include <optional>

class QSocketNotifier;

namespace Wheelstar::Device
{
    /// One wheel: the settings its driver exposes in sysfs, and its event node for input and force feedback.
    ///
    /// The sysfs half works without the event node being open; the input and force feedback half needs open().
    /// Force feedback calls are made from the test thread as well as the GUI thread - they are single ioctls and
    /// writes on the file descriptor, which the kernel serialises.
    class WheelDevice : public QObject
    {
        Q_OBJECT

    public:
        struct Mode
        {
            QString id;
            QString name;
            bool active = false;
        };

        WheelDevice(QString id, QString name, QString eventNode, QString sysfsPath, const WheelModel& model,
                    QObject* parent = nullptr);
        ~WheelDevice() override;

        WheelDevice(const WheelDevice&) = delete ("owns a file descriptor");
        WheelDevice& operator=(const WheelDevice&) = delete ("owns a file descriptor");
        WheelDevice(WheelDevice&&) = delete ("owns a file descriptor, and its QObject children point at it");
        WheelDevice& operator=(WheelDevice&&) = delete ("owns a file descriptor, and its QObject children point at it");

        [[nodiscard]] const QString& id() const;
        [[nodiscard]] const QString& name() const;
        [[nodiscard]] const QString& eventNode() const;
        [[nodiscard]] int maxRange() const;

        /// False between a hotplug and the moment the driver has finished setting the device up.
        [[nodiscard]] bool isReady() const;
        void setReady(bool ready);

        /// The emulation modes the driver offers, without "native"; empty when it offers none.
        [[nodiscard]] QList<Mode> modes() const;
        [[nodiscard]] std::optional<QString> mode() const;
        /// Switching the mode makes the wheel disconnect and come back, possibly as another product.
        bool setMode(const QString& mode);

        /// A plain integer attribute in sysfs: range, combine_pedals, spring_level, damper_level, friction_level,
        /// ffb_leds, peak_ffb_level. Empty when the driver does not offer it or it cannot be read and written.
        std::optional<int> attribute(const char* name) const;
        bool setAttribute(const char* name, int value);

        /// Percent. Through sysfs when the driver offers it, otherwise through the input layer, which cannot be read.
        std::optional<int> ffGain();
        bool setFfGain(int percent);
        std::optional<int> autocenter();
        bool setAutocenter(int percent);

        /// Whether every settings file present, and the event node, can be read and written by this user.
        [[nodiscard]] bool hasPermissions() const;

        bool open();
        void close();
        [[nodiscard]] bool isOpen() const;
        bool setGrabbed(bool grabbed);

        /// The last steering position, -1 (full left) to 1 (full right).
        [[nodiscard]] double steering() const;

        /// Uploads a constant force effect, or updates the effect `effectId` when it is not -1. Returns its id,
        /// -1 on failure. `direction` 0x4000 pulls left, 0xc000 right.
        int uploadConstantEffect(int effectId, std::uint16_t direction, std::int16_t level);
        void playEffect(int effectId, bool play);
        void eraseEffect(int effectId);

    Q_SIGNALS:
        /// `timestampUs` is CLOCK_MONOTONIC, the clock std::chrono::steady_clock reads.
        void steeringChanged(double position, qint64 timestampUs);
        /// `axis` in Oversteer's layout (ABS_Z throttle, ABS_RZ brake, ABS_Y clutch), `pressed` 0 to 1.
        void pedalChanged(int axis, double pressed);
        void hatChanged(int axis, int value);
        void buttonChanged(int index, bool pressed);

    private:
        QString sysfsFile(const char* name) const;
        [[nodiscard]] bool hasForceFeedback(int code) const;
        bool writeEvent(std::uint16_t type, std::uint16_t code, std::int32_t value);
        void readEvents();
        void handleEvent(const input_event& event);
        [[nodiscard]] double normalized(int code, int value) const;

        QString m_id;
        QString m_name;
        QString m_eventNode;
        QString m_sysfsPath;
        WheelModel m_model;
        bool m_ready = true;

        int m_fd = -1;
        QSocketNotifier* m_notifier = nullptr;
        std::array<input_absinfo, ABS_CNT> m_absInfo{};
        std::atomic<double> m_steering = 0.0;
    };
} // namespace Wheelstar::Device
