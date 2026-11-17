// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device/wheeldevice.h"
#include "core/logging.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSocketNotifier>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <span>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace Wheelstar::Device
{
    namespace
    {
        /// The driver's gain and autocenter files count in 16 bits, 0 to 65535, where the app shows percent. Going
        /// either way is a rule of three: percent * 65535 / 100, and value * 100 / 65535 rounded to the nearest.
        constexpr int SysfsFull = 65535;

        /// Every settings file the driver may offer. Only the ones present are required to be writable.
        constexpr std::array SettingsFiles{"alternate_modes", "range",         "combine_pedals", "gain",
                                           "autocenter",      "spring_level",  "damper_level",   "friction_level",
                                           "ffb_leds",        "peak_ffb_level"};

        std::optional<QByteArray> readFile(const QString& path)
        {
            QFile file(path);

            if (!file.open(QIODevice::ReadOnly))
            {
                return std::nullopt;
            }

            return file.readAll();
        }

        bool writeFile(const QString& path, const QByteArray& data)
        {
            QFile file(path);

            if (!file.open(QIODevice::WriteOnly))
            {
                qCWarning(WHEELSTAR) << "Cannot write" << path << file.errorString();
                return false;
            }

            return file.write(data) == data.size();
        }

        constexpr bool testBit(std::span<const unsigned long> bits, std::size_t bit)
        {
            constexpr std::size_t BitsPerWord = sizeof(unsigned long) * 8;
            return ((bits.at(bit / BitsPerWord) >> (bit % BitsPerWord)) & 1UL) != 0;
        }
    } // namespace

    WheelDevice::WheelDevice(QString id, QString name, QString eventNode, QString sysfsPath, const WheelModel& model,
                             QObject* parent)
        : QObject(parent), m_id(std::move(id)), m_name(std::move(name)), m_eventNode(std::move(eventNode)),
          m_sysfsPath(std::move(sysfsPath)), m_model(model)
    {
    }

    WheelDevice::~WheelDevice()
    {
        close();
    }

    const QString& WheelDevice::id() const
    {
        return m_id;
    }

    const QString& WheelDevice::name() const
    {
        return m_name;
    }

    const QString& WheelDevice::eventNode() const
    {
        return m_eventNode;
    }

    int WheelDevice::maxRange() const
    {
        return m_model.maxRange;
    }

    bool WheelDevice::isReady() const
    {
        return m_ready;
    }

    void WheelDevice::setReady(bool ready)
    {
        m_ready = ready;
    }

    QString WheelDevice::sysfsFile(const char* name) const
    {
        return m_sysfsPath + u'/' + QLatin1StringView(name);
    }

    QList<WheelDevice::Mode> WheelDevice::modes() const
    {
        // One line per mode, "G29: G29 Racing Wheel *", the asterisk marking the active one.
        static const QRegularExpression line(u"^([^:]+): (.*?)( \\*)?$"_s);
        const auto data = readFile(sysfsFile("alternate_modes"));
        QList<Mode> result;

        if (!data)
        {
            return result;
        }

        for (const QString& text : QString::fromUtf8(*data).split(u'\n', Qt::SkipEmptyParts))
        {
            if (const auto match = line.match(text.trimmed()); match.hasMatch() && match.captured(1) != "native"_L1)
            {
                result.append({.id = match.captured(1), .name = match.captured(2), .active = match.hasCaptured(3)});
            }
        }

        return result;
    }

    std::optional<QString> WheelDevice::mode() const
    {
        for (const Mode& mode : modes())
        {
            if (mode.active)
            {
                return mode.id;
            }
        }

        return std::nullopt;
    }

    bool WheelDevice::setMode(const QString& mode)
    {
        if (this->mode() == mode)
        {
            return true;
        }

        qCDebug(WHEELSTAR) << "Setting mode" << mode;
        return writeFile(sysfsFile("alternate_modes"), mode.toUtf8());
    }

    std::optional<int> WheelDevice::attribute(const char* name) const
    {
        const QString path = sysfsFile(name);

        if (!QFileInfo(path).isWritable())
        {
            return std::nullopt;
        }

        const auto data = readFile(path);
        bool ok = false;
        const int value = data ? data->trimmed().toInt(&ok) : 0;

        return ok ? std::optional(value) : std::nullopt;
    }

    bool WheelDevice::setAttribute(const char* name, int value)
    {
        const QString path = sysfsFile(name);

        if (!QFileInfo(path).isWritable())
        {
            return false;
        }

        qCDebug(WHEELSTAR) << "Setting" << name << value;
        return writeFile(path, QByteArray::number(value));
    }

    std::optional<int> WheelDevice::ffGain()
    {
        if (const auto gain = attribute("gain"))
        {
            return static_cast<int>(std::lround(*gain * 100.0 / SysfsFull));
        }

        // The input layer cannot report the gain, and a fresh device starts at full gain.
        return hasForceFeedback(FF_GAIN) ? std::optional(100) : std::nullopt;
    }

    bool WheelDevice::setFfGain(int percent)
    {
        const int value = std::clamp(percent, 0, 100) * SysfsFull / 100;
        return setAttribute("gain", value) || writeEvent(EV_FF, FF_GAIN, value);
    }

    std::optional<int> WheelDevice::autocenter()
    {
        if (const auto autocenter = attribute("autocenter"))
        {
            return static_cast<int>(std::lround(*autocenter * 100.0 / SysfsFull));
        }

        return hasForceFeedback(FF_AUTOCENTER) ? std::optional(0) : std::nullopt;
    }

    bool WheelDevice::setAutocenter(int percent)
    {
        const int value = std::clamp(percent, 0, 100) * SysfsFull / 100;
        return setAttribute("autocenter", value) || writeEvent(EV_FF, FF_AUTOCENTER, value);
    }

    bool WheelDevice::hasPermissions() const
    {
        if (const QFileInfo directory(m_sysfsPath); !directory.isReadable() || !directory.isExecutable())
        {
            return false;
        }

        for (const char* name : SettingsFiles)
        {
            if (const QFileInfo file(sysfsFile(name)); file.exists() && (!file.isReadable() || !file.isWritable()))
            {
                qCDebug(WHEELSTAR) << "No access to" << file.filePath();
                return false;
            }
        }

        return QFileInfo(m_eventNode).isReadable();
    }

    bool WheelDevice::open()
    {
        if (m_fd >= 0)
        {
            return true;
        }

        const QByteArray path = QFile::encodeName(m_eventNode);
        m_fd = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);

        if (m_fd < 0)
        {
            // Input still works without write access; only force feedback through the input layer does not.
            m_fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        }

        if (m_fd < 0)
        {
            qCWarning(WHEELSTAR) << "Cannot open" << m_eventNode << strerror(errno);
            return false;
        }

        // The test compares event times with std::chrono::steady_clock, so both have to be the monotonic clock.
        int clock = CLOCK_MONOTONIC;
        ::ioctl(m_fd, EVIOCSCLOCKID, &clock);

        for (int code = 0; code < ABS_CNT; ++code)
        {
            ::ioctl(m_fd, EVIOCGABS(code), &m_absInfo.at(static_cast<std::size_t>(code)));
        }

        m_steering = normalized(ABS_X, m_absInfo.at(ABS_X).value);
        m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &WheelDevice::readEvents);
        return true;
    }

    void WheelDevice::close()
    {
        delete m_notifier;
        m_notifier = nullptr;

        if (m_fd >= 0)
        {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    bool WheelDevice::isOpen() const
    {
        return m_fd >= 0;
    }

    bool WheelDevice::setGrabbed(bool grabbed)
    {
        return m_fd >= 0 && ::ioctl(m_fd, EVIOCGRAB, grabbed ? 1 : 0) == 0;
    }

    double WheelDevice::steering() const
    {
        return m_steering;
    }

    bool WheelDevice::hasForceFeedback(int code) const
    {
        std::array<unsigned long, (FF_CNT + 63) / 64> bits{};

        if (m_fd < 0 || ::ioctl(m_fd, EVIOCGBIT(EV_FF, sizeof(bits)), bits.data()) < 0)
        {
            return false;
        }

        return testBit(bits, static_cast<std::size_t>(code));
    }

    bool WheelDevice::writeEvent(std::uint16_t type, std::uint16_t code, std::int32_t value)
    {
        input_event event{};
        event.type = type;
        event.code = code;
        event.value = value;

        return m_fd >= 0 && ::write(m_fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
    }

    int WheelDevice::uploadConstantEffect(int effectId, std::uint16_t direction, std::int16_t level)
    {
        ff_effect effect{};
        effect.type = FF_CONSTANT;
        effect.id = static_cast<std::int16_t>(effectId);
        effect.direction = direction;
        effect.u.constant.level = level;

        if (m_fd < 0 || ::ioctl(m_fd, EVIOCSFF, &effect) < 0)
        {
            qCWarning(WHEELSTAR) << "Cannot upload a force feedback effect:" << strerror(errno);
            return -1;
        }

        return effect.id;
    }

    void WheelDevice::playEffect(int effectId, bool play)
    {
        if (effectId >= 0)
        {
            writeEvent(EV_FF, static_cast<std::uint16_t>(effectId), play ? 1 : 0);
        }
    }

    void WheelDevice::eraseEffect(int effectId)
    {
        if (m_fd >= 0 && effectId >= 0)
        {
            ::ioctl(m_fd, EVIOCRMFF, effectId);
        }
    }

    double WheelDevice::normalized(int code, int value) const
    {
        const input_absinfo& info = m_absInfo.at(static_cast<std::size_t>(code));

        // A degenerate range (the device reports none) would divide by zero.
        if (info.maximum <= info.minimum)
        {
            return 0.0;
        }

        // The position within the axis range as 0 to 1, whatever range the axis reports (minimum and maximum come
        // from the device itself).
        return static_cast<double>(value - info.minimum) / (info.maximum - info.minimum);
    }

    void WheelDevice::readEvents()
    {
        std::array<input_event, 64> events{};

        while (true)
        {
            const ssize_t count = ::read(m_fd, events.data(), sizeof(events));

            if (count < 0)
            {
                if (errno == ENODEV)
                {
                    // Unplugged, or re-enumerating after a mode change. The device manager hears of it from udev.
                    close();
                }

                return;
            }

            for (const input_event& event :
                 std::span(events).first(static_cast<std::size_t>(count) / sizeof(input_event)))
            {
                handleEvent(event);
            }

            if (static_cast<std::size_t>(count) < sizeof(events))
            {
                return;
            }
        }
    }

    void WheelDevice::handleEvent(const input_event& event)
    {
        if (event.type == EV_KEY)
        {
            if (const int index = buttonIndex(m_model.layout, event.code); index >= 0 && event.value != 2)
            {
                Q_EMIT buttonChanged(index, event.value != 0);
            }

            return;
        }

        if (event.type != EV_ABS)
        {
            return;
        }

        switch (const double position = normalized(event.code, event.value); remapAxis(m_model.layout, event.code))
        {
            case ABS_X:
            {
                // 0 to 1 becomes -1 (full left) to 1 (full right), with 0 in the middle.
                m_steering = (position * 2.0) - 1.0;
                // The event time is a second count and a microsecond count; together, microseconds.
                Q_EMIT steeringChanged(
                    m_steering, (static_cast<qint64>(event.input_event_sec) * 1'000'000) + event.input_event_usec);
                break;
            }
            case ABS_Y:
            case ABS_Z:
            case ABS_RZ:
                // Some layouts report a pedal at its maximum when released; 1 - position turns it around so that
                // 0 is released and 1 is pressed on every wheel.
                Q_EMIT pedalChanged(remapAxis(m_model.layout, event.code),
                                    pedalsInverted(m_model.layout) ? 1.0 - position : position);
                break;
            case ABS_HAT0X:
            case ABS_HAT0Y:
                Q_EMIT hatChanged(event.code, event.value);
                break;
            default:
                break;
        }
    }
} // namespace Wheelstar::Device
