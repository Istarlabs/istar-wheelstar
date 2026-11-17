// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "settings/buttonnames.h"

#include <toml++/toml.hpp>

#include <string>

/// Button labels as a TOML table, the button number being the key. Private to the two files that read and write
/// them; it is a header only so that toml++ stays out of the public ones.
namespace Wheelstar::Settings
{
    inline ButtonLabels labelsFromToml(const toml::table& table)
    {
        ButtonLabels labels;

        for (const auto& [key, node] : table)
        {
            bool ok = false;
            const int index = QString::fromStdString(std::string(key.str())).toInt(&ok);
            if (const auto text = node.value<std::string>(); ok && text && !text->empty())
            {
                labels.insert(index, QString::fromStdString(*text));
            }
        }

        return labels;
    }

    inline toml::table labelsToToml(const ButtonLabels& labels)
    {
        toml::table table;

        for (auto entry = labels.cbegin(); entry != labels.cend(); ++entry)
        {
            table.insert(std::to_string(entry.key()), entry.value().toStdString());
        }

        return table;
    }
} // namespace Wheelstar::Settings
