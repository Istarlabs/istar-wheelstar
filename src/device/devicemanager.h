// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QObject>

#include <memory>

class QSocketNotifier;
struct udev;
struct udev_device;
struct udev_monitor;

namespace Wheelstar::Device
{
    class WheelDevice;

    /// The known wheels that are plugged in, kept current through udev.
    class DeviceManager : public QObject
    {
        Q_OBJECT

    public:
        explicit DeviceManager(QObject* parent = nullptr);
        ~DeviceManager() override;

        DeviceManager(const DeviceManager&) = delete ("owns the udev handles");
        DeviceManager& operator=(const DeviceManager&) = delete ("owns the udev handles");
        DeviceManager(DeviceManager&&) = delete ("owns the udev handles");
        DeviceManager& operator=(DeviceManager&&) = delete ("owns the udev handles");

        /// Lists what is plugged in now, then listens for changes.
        void start();

        [[nodiscard]] QList<WheelDevice*> devices() const;
        /// By udev device path, or by event node ("/dev/input/event16").
        [[nodiscard]] WheelDevice* device(const QString& idOrNode) const;

    Q_SIGNALS:
        /// A device was added, became ready or is gone. A removed device is deleted once control returns to the
        /// event loop, so a pointer held across this signal has to be dropped in the handler.
        void devicesChanged();

    private:
        void readMonitor();
        WheelDevice* addDevice(udev_device* device);

        std::unique_ptr<udev, udev* (*)(udev*)> m_udev;
        std::unique_ptr<udev_monitor, udev_monitor* (*)(udev_monitor*)> m_monitor;
        QSocketNotifier* m_notifier = nullptr;
        QList<WheelDevice*> m_devices;
    };
} // namespace Wheelstar::Device
