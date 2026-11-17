// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "settings/buttonnames.h"

#include <QString>
#include <QVariantMap>

#include <expected>
#include <optional>

namespace Wheelstar::Device
{
    class WheelDevice;
} // namespace Wheelstar::Device

namespace Wheelstar::Settings
{
    /// The settings a profile holds. An empty value means "leave as it is": a setting the wheel does not support,
    /// or one a profile does not mention.
    struct WheelSettings
    {
        std::optional<QString> mode = std::nullopt;
        std::optional<int> range = std::nullopt;
        std::optional<int> ffGain = std::nullopt;
        std::optional<int> autocenter = std::nullopt;
        std::optional<int> combinePedals = std::nullopt;
        std::optional<int> springLevel = std::nullopt;
        std::optional<int> damperLevel = std::nullopt;
        std::optional<int> frictionLevel = std::nullopt;
        std::optional<bool> ffbLeds = std::nullopt;
        std::optional<bool> ffbOverlay = std::nullopt;
        /// "never", "always" or "auto".
        std::optional<QString> rangeOverlay = std::nullopt;
        std::optional<bool> useButtons = std::nullopt;
        std::optional<bool> centerWheel = std::nullopt;
        std::optional<bool> startAppManually = std::nullopt;
        /// What each button does in the game this profile is for ("Horn"); saved as the `[button_notes]` table.
        // The initializer looks redundant, but without one every designated initializer above has to name the member.
        ButtonLabels buttonNotes = {}; // NOLINT(readability-redundant-member-init)

        bool operator==(const WheelSettings&) const = default;

        /// What the wheel is set to now.
        static WheelSettings fromDevice(Device::WheelDevice& device);

        /// Takes every value `other` has.
        void overrideWith(const WheelSettings& other);

        /// By profile key ("range", "ff_gain", ...). The map leaves out what is empty, so QML reads it as undefined.
        [[nodiscard]] QVariantMap toVariantMap() const;
        /// False for an unknown key or a value of the wrong type.
        bool setValue(const QString& key, const QVariant& value);

        /// Writes everything but the mode and the centring to the wheel; those two need the event loop.
        void applyTo(Device::WheelDevice& device) const;

        static std::expected<WheelSettings, QString> load(const QString& path);
        std::expected<void, QString> save(const QString& path) const;
    };
} // namespace Wheelstar::Settings
