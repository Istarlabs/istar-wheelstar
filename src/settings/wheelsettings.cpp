// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings/wheelsettings.h"
#include "core/inifile.h"
#include "core/logging.h"
#include "device/wheeldevice.h"
#include "settings/buttonlabelstoml.h"

#include <KLocalizedString>

#include <QSaveFile>

#include <toml++/toml.hpp>

#include <array>
#include <sstream>
#include <variant>

using namespace Qt::StringLiterals;

namespace Wheelstar::Settings
{
    namespace
    {
        using Field = std::variant<std::optional<QString> WheelSettings::*, std::optional<int> WheelSettings::*,
                                   std::optional<bool> WheelSettings::*>;

        struct Key
        {
            const char* name;
            Field field;
        };

        /// The key names are upstream Oversteer's, so a profile reads the same in both programs.
        constexpr std::array Keys{
            Key{.name = "mode", .field = &WheelSettings::mode},
            Key{.name = "range", .field = &WheelSettings::range},
            Key{.name = "ff_gain", .field = &WheelSettings::ffGain},
            Key{.name = "autocenter", .field = &WheelSettings::autocenter},
            Key{.name = "combine_pedals", .field = &WheelSettings::combinePedals},
            Key{.name = "spring_level", .field = &WheelSettings::springLevel},
            Key{.name = "damper_level", .field = &WheelSettings::damperLevel},
            Key{.name = "friction_level", .field = &WheelSettings::frictionLevel},
            Key{.name = "ffb_leds", .field = &WheelSettings::ffbLeds},
            Key{.name = "ffb_overlay", .field = &WheelSettings::ffbOverlay},
            Key{.name = "range_overlay", .field = &WheelSettings::rangeOverlay},
            Key{.name = "use_buttons", .field = &WheelSettings::useButtons},
            Key{.name = "center_wheel", .field = &WheelSettings::centerWheel},
            Key{.name = "start_app_manually", .field = &WheelSettings::startAppManually},
        };

        /// The standard overload set for std::visit: one lambda per alternative.
        template <typename... Functions>
        struct Overloaded : Functions... // NOLINT(misc-multiple-inheritance): stateless lambdas, by design of the idiom
        {
            using Functions::operator()...;
        };

        std::expected<WheelSettings, QString> loadToml(const QString& path)
        {
            toml::table table;

            try
            {
                table = toml::parse_file(path.toStdString());
            }
            catch (const toml::parse_error& error)
            {
                return std::unexpected(
                    i18n("\"%1\" is not a valid profile: %2", path, QString::fromUtf8(error.what())));
            }

            WheelSettings settings;

            for (const auto& [name, field] : Keys)
            {
                // toml++'s operator[] is a lookup, not an index: a missing key gives an empty view, which reads as no
                // value.
                const toml::node_view node =
                    table[name]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

                std::visit(
                    Overloaded{
                        [&](std::optional<QString> WheelSettings::* member)
                        {
                            if (const auto value = node.value<std::string>())
                            {
                                settings.*member = QString::fromStdString(*value);
                            }
                        },
                        [&](std::optional<int> WheelSettings::* member) { settings.*member = node.value<int>(); },
                        [&](std::optional<bool> WheelSettings::* member) { settings.*member = node.value<bool>(); },
                    },
                    field);
            }

            if (const auto* const notes =
                    table["button_notes"] // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                        .as_table())
            {
                settings.buttonNotes = labelsFromToml(*notes);
            }

            return settings;
        }

        /// An upstream profile: every value a string, booleans written as 0 and 1.
        std::expected<WheelSettings, QString> loadIni(const QString& path)
        {
            const auto entries = Core::IniFile::read(path);

            if (!entries)
            {
                return std::unexpected(entries.error());
            }

            WheelSettings settings;

            for (const auto& [key, text] : *entries)
            {
                const auto* const found =
                    std::ranges::find_if(Keys, [&](const Key& entry) { return key == QLatin1StringView(entry.name); });

                if (found == Keys.end())
                {
                    qCWarning(WHEELSTAR) << "Unknown profile setting" << key << "in" << path;
                    continue;
                }

                bool ok = false;
                const int number = text.toInt(&ok);

                std::visit(
                    Overloaded{
                        [&](std::optional<QString> WheelSettings::* member) { settings.*member = text; },
                        [&](std::optional<int> WheelSettings::* member)
                        {
                            if (ok)
                            {
                                settings.*member = number;
                            }
                        },
                        [&](std::optional<bool> WheelSettings::* member)
                        {
                            if (ok)
                            {
                                settings.*member = number != 0;
                            }
                        },
                    },
                    found->field);
            }

            return settings;
        }
    } // namespace

    WheelSettings WheelSettings::fromDevice(Device::WheelDevice& device)
    {
        const bool hasRange = device.attribute("range").has_value();
        const bool hasFfbMeter = device.attribute("peak_ffb_level").has_value();

        return {
            .mode = device.mode(),
            .range = device.attribute("range"),
            .ffGain = device.ffGain(),
            .autocenter = device.autocenter(),
            .combinePedals = device.attribute("combine_pedals"),
            .springLevel = device.attribute("spring_level"),
            .damperLevel = device.attribute("damper_level"),
            .frictionLevel = device.attribute("friction_level"),
            .ffbLeds = device.attribute("ffb_leds").transform([](int value) { return value != 0; }),
            .ffbOverlay = hasFfbMeter ? std::optional(false) : std::nullopt,
            // Upstream made this depend on the FFB meter; it shows the range, so it depends on the range.
            .rangeOverlay = hasRange ? std::optional(u"never"_s) : std::nullopt,
            .useButtons = hasRange ? std::optional(false) : std::nullopt,
            .centerWheel = false,
            .startAppManually = false,
        };
    }

    void WheelSettings::overrideWith(const WheelSettings& other)
    {
        for (const auto& [_, field] : Keys)
        {
            std::visit(
                [&](auto member)
                {
                    if ((other.*member).has_value())
                    {
                        this->*member = other.*member;
                    }
                },
                field);
        }

        if (!other.buttonNotes.isEmpty())
        {
            buttonNotes = other.buttonNotes;
        }
    }

    QVariantMap WheelSettings::toVariantMap() const
    {
        QVariantMap map;

        if (!buttonNotes.isEmpty())
        {
            // Keyed by the number as text: that is what a JavaScript object has.
            QVariantMap notes;

            for (auto note = buttonNotes.cbegin(); note != buttonNotes.cend(); ++note)
            {
                notes.insert(QString::number(note.key()), note.value());
            }

            map.insert(u"button_notes"_s, notes);
        }

        for (const auto& [name, field] : Keys)
        {
            std::visit(
                [&](auto member)
                {
                    if (const auto& value = this->*member)
                    {
                        map.insert(QLatin1StringView(name), QVariant::fromValue(*value));
                    }
                },
                field);
        }

        return map;
    }

    bool WheelSettings::setValue(const QString& key, const QVariant& value)
    {
        const auto* const found =
            std::ranges::find_if(Keys, [&](const Key& entry) { return key == QLatin1StringView(entry.name); });

        if (found == Keys.end())
        {
            return false;
        }

        return std::visit(
            Overloaded{
                [&](std::optional<QString> WheelSettings::* member)
                {
                    this->*member = value.toString();
                    return true;
                },
                [&](std::optional<int> WheelSettings::* member)
                {
                    bool ok = false;
                    const int number = value.toInt(&ok);
                    this->*member = ok ? std::optional(number) : this->*member;
                    return ok;
                },
                [&](std::optional<bool> WheelSettings::* member)
                {
                    this->*member = value.toBool();
                    return true;
                },
            },
            found->field);
    }

    void WheelSettings::applyTo(Device::WheelDevice& device) const
    {
        if (range)
        {
            // The same lower limit as Controller's MinimumRange; the upper one is this wheel's own, because a saved
            // profile may come from a wheel with a larger range.
            device.setAttribute("range", std::clamp(*range, 40, device.maxRange()));
        }

        if (combinePedals)
        {
            device.setAttribute("combine_pedals", *combinePedals);
        }

        if (autocenter)
        {
            device.setAutocenter(*autocenter);
        }

        if (ffGain)
        {
            device.setFfGain(*ffGain);
        }

        if (springLevel)
        {
            device.setAttribute("spring_level", *springLevel);
        }

        if (damperLevel)
        {
            device.setAttribute("damper_level", *damperLevel);
        }

        if (frictionLevel)
        {
            device.setAttribute("friction_level", *frictionLevel);
        }

        if (ffbLeds)
        {
            device.setAttribute("ffb_leds", *ffbLeds ? 1 : 0);
        }
    }

    std::expected<WheelSettings, QString> WheelSettings::load(const QString& path)
    {
        return path.endsWith(".ini"_L1) ? loadIni(path) : loadToml(path);
    }

    std::expected<void, QString> WheelSettings::save(const QString& path) const
    {
        toml::table table;

        for (const auto& [name, field] : Keys)
        {
            std::visit(
                Overloaded{
                    [&](std::optional<QString> WheelSettings::* member)
                    {
                        if (const auto& value = this->*member)
                        {
                            table.insert(name, value->toStdString());
                        }
                    },
                    [&](auto member)
                    {
                        if (const auto& value = this->*member)
                        {
                            table.insert(name, *value);
                        }
                    },
                },
                field);
        }

        if (!buttonNotes.isEmpty())
        {
            // After the plain values: a table has to come last, or what follows it would belong to it.
            table.insert("button_notes", labelsToToml(buttonNotes));
        }

        std::ostringstream stream;
        stream << table << '\n';

        if (QSaveFile file(path); !file.open(QIODevice::WriteOnly) ||
            file.write(QByteArray::fromStdString(stream.str())) < 0 || !file.commit())
        {
            return std::unexpected(i18n("Cannot write \"%1\": %2", path, file.errorString()));
        }

        return {};
    }
} // namespace Wheelstar::Settings
