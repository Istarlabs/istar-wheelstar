// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <expected>
#include <utility>

/// Reads the `[DEFAULT]` section Python's configparser writes - upstream Oversteer's profile format. Only
/// there to bring those profiles over; this program itself writes TOML.
namespace Wheelstar::Core::IniFile
{
    using Entries = QList<std::pair<QString, QString>>;

    std::expected<Entries, QString> read(const QString& path);
} // namespace Wheelstar::Core::IniFile
