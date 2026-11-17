// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/devicemanager.h"
#include "core/logging.h"
#include "device/wheeldevice.h"

#include <QDir>
#include <QSocketNotifier>
#include <QTimer>

#include <libudev.h>

using namespace Qt::StringLiterals;

namespace Wheelstar::Device
{
    namespace
    {
        /// How long a newly plugged wheel is left alone. The driver creates its sysfs files after the event
        /// node appears, and a mode switch takes a few seconds to settle.
        constexpr auto ReadyDelay = std::chrono::seconds(5);

        QString udevProperty(udev_device* device, const char* name)
        {
            return QString::fromUtf8(udev_device_get_property_value(device, name));
        }
    } // namespace

    DeviceManager::DeviceManager(QObject* parent)
        : QObject(parent), m_udev(udev_new(), &udev_unref), m_monitor(nullptr, &udev_monitor_unref)
    {
    }

    DeviceManager::~DeviceManager() = default;

    void DeviceManager::start()
    {
        if (!m_udev)
        {
            qCWarning(WHEELSTAR) << "udev is not available";
            return;
        }

        std::unique_ptr<udev_enumerate, udev_enumerate* (*)(udev_enumerate*)> const enumerate(
            udev_enumerate_new(m_udev.get()), &udev_enumerate_unref);
        udev_enumerate_add_match_subsystem(enumerate.get(), "input");
        udev_enumerate_add_match_property(enumerate.get(), "ID_INPUT_JOYSTICK", "1");
        udev_enumerate_scan_devices(enumerate.get());

        for (udev_list_entry* entry = udev_enumerate_get_list_entry(enumerate.get()); entry != nullptr;
             entry = udev_list_entry_get_next(entry))
        {
            udev_device* device = udev_device_new_from_syspath(m_udev.get(), udev_list_entry_get_name(entry));
            addDevice(device);
            udev_device_unref(device);
        }

        m_monitor.reset(udev_monitor_new_from_netlink(m_udev.get(), "udev"));
        udev_monitor_filter_add_match_subsystem_devtype(m_monitor.get(), "input", nullptr);
        udev_monitor_enable_receiving(m_monitor.get());

        m_notifier = new QSocketNotifier(udev_monitor_get_fd(m_monitor.get()), QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &DeviceManager::readMonitor);
    }

    QList<WheelDevice*> DeviceManager::devices() const
    {
        return m_devices;
    }

    WheelDevice* DeviceManager::device(const QString& idOrNode) const
    {
        const auto found =
            std::ranges::find_if(m_devices, [&](const WheelDevice* device)
                                 { return device->id() == idOrNode || device->eventNode() == idOrNode; });

        return found == m_devices.end() ? nullptr : *found;
    }

    WheelDevice* DeviceManager::addDevice(udev_device* device)
    {
        const char* node = udev_device_get_devnode(device);
        const QString id = QString::fromUtf8(udev_device_get_devpath(device));

        if (node == nullptr || !QString::fromUtf8(node).contains("event"_L1) || this->device(id) != nullptr)
        {
            return nullptr;
        }

        const WheelModel* model = findWheelModel(udevProperty(device, "ID_VENDOR_ID").toUShort(nullptr, 16),
                                                 udevProperty(device, "ID_MODEL_ID").toUShort(nullptr, 16));

        if (model == nullptr)
        {
            return nullptr;
        }

        // event16 -> input23 (which carries the name) -> the HID device, where the driver puts its settings.
        const QString sysPath = QString::fromUtf8(udev_device_get_syspath(device));
        const QString hidPath = QDir(sysPath + "/device/device"_L1).canonicalPath();
        udev_device* input = udev_device_get_parent(device);
        const char* name = input != nullptr ? udev_device_get_sysattr_value(input, "name") : nullptr;

        auto* wheel = new WheelDevice(id, name != nullptr ? QString::fromUtf8(name) : udevProperty(device, "ID_MODEL"),
                                      QString::fromUtf8(node), hidPath, *model, this);
        m_devices.append(wheel);
        qCDebug(WHEELSTAR) << "Found" << wheel->name() << "at" << wheel->eventNode() << hidPath;
        return wheel;
    }

    void DeviceManager::readMonitor()
    {
        std::unique_ptr<udev_device, udev_device* (*)(udev_device*)> const device(
            udev_monitor_receive_device(m_monitor.get()), &udev_device_unref);

        if (!device)
        {
            return;
        }

        const QString action = QString::fromUtf8(udev_device_get_action(device.get()));
        const QString id = QString::fromUtf8(udev_device_get_devpath(device.get()));
        qCDebug(WHEELSTAR) << "udev" << action << id;

        if (action == "add"_L1)
        {
            if (WheelDevice* wheel = addDevice(device.get()))
            {
                wheel->setReady(false);
                QTimer::singleShot(ReadyDelay, wheel,
                                   [this, wheel]
                                   {
                                       wheel->setReady(true);
                                       Q_EMIT devicesChanged();
                                   });
                Q_EMIT devicesChanged();
            }
        }
        else if (action == "remove"_L1)
        {
            if (WheelDevice* wheel = this->device(id))
            {
                m_devices.removeOne(wheel);
                wheel->close();
                wheel->deleteLater();
                Q_EMIT devicesChanged();
            }
        }
    }
} // namespace Wheelstar::Device
