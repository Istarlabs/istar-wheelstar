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
    /// Empty when the file or the wheel is not there.
    [[nodiscard]] ButtonLabels loadButtonNames(const QString& path, const QString& wheel);

    /// Replaces the names of one wheel and leaves the others in the file alone. A file that cannot be parsed is not
    /// overwritten.
    std::expected<void, QString> saveButtonNames(const QString& path, const QString& wheel, const ButtonLabels& names);
} // namespace Wheelstar::Settings
