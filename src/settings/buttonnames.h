// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QMap>
#include <QString>

#include <expected>

namespace Wheelstar::Settings
{
    /// A text per button number.
    using ButtonLabels = QMap<int, QString>;

    /// The names the user gave the physical buttons of a wheel ("X", "Left paddle"), by the wheel's model name. The
    /// games only see button numbers; this is how the user tells which is which. One TOML file holds every wheel.
    /// A profile may name the buttons its own way (a game's buttons do not do what another's do); a profile without
    /// names of its own - or none given - gets the wheel's. Empty when the file or the wheel is not there.
    [[nodiscard]] ButtonLabels loadButtonNames(const QString& path, const QString& wheel, const QString& profile = {});

    /// Replaces the names of one wheel - or, with a profile, of that profile only - and leaves the rest of the file
    /// alone. A file that cannot be parsed is not overwritten.
    std::expected<void, QString> saveButtonNames(const QString& path, const QString& wheel, const QString& profile,
                                                 const ButtonLabels& names);
} // namespace Wheelstar::Settings
