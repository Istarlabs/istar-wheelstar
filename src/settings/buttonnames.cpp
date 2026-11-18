// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings/buttonnames.h"
#include "settings/buttonlabelstoml.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <sstream>
#include <string_view>

namespace Wheelstar::Settings
{
    namespace
    {
        /// The key of the wheel's table that holds the per-profile tables; not a number, so the wheel-level reader
        /// skips it.
        constexpr std::string_view ProfilesKey = "profiles";
    } // namespace

    ButtonLabels loadButtonNames(const QString& path, const QString& wheel, const QString& profile)
    {
        if (!QFile::exists(path))
        {
            return {};
        }

        try
        {
            const toml::table table = toml::parse_file(path.toStdString());
            const auto* const names = table.get_as<toml::table>(wheel.toStdString());

            if (names == nullptr)
            {
                return {};
            }

            if (!profile.isEmpty())
            {
                const auto* const profiles = names->get_as<toml::table>(ProfilesKey);

                if (const auto* const own =
                        profiles != nullptr ? profiles->get_as<toml::table>(profile.toStdString()) : nullptr;
                    own != nullptr)
                {
                    if (const ButtonLabels labels = labelsFromToml(*own); !labels.isEmpty())
                    {
                        return labels;
                    }
                }
            }

            return labelsFromToml(*names);
        }
        catch (const toml::parse_error&)
        {
            return {};
        }
    }

    std::expected<void, QString> saveButtonNames(const QString& path, const QString& wheel, const QString& profile,
                                                 const ButtonLabels& names)
    {
        toml::table table;

        if (QFile::exists(path))
        {
            try
            {
                table = toml::parse_file(path.toStdString());
            }
            catch (const toml::parse_error& error)
            {
                return std::unexpected(i18n("\"%1\" cannot be read: %2", path, QString::fromUtf8(error.what())));
            }
        }

        const std::string wheelKey = wheel.toStdString();

        // The wheel's table is rebuilt from its own part (numbers) and its profiles; whichever one is being saved is
        // replaced, the other is carried over unchanged.
        toml::table updated = profile.isEmpty() ? labelsToToml(names) : toml::table();
        toml::table profiles;

        if (const auto* const existing = table.get_as<toml::table>(wheelKey))
        {
            if (!profile.isEmpty())
            {
                updated = labelsToToml(labelsFromToml(*existing));
            }

            if (const auto* const existingProfiles = existing->get_as<toml::table>(ProfilesKey))
            {
                profiles = *existingProfiles;
            }
        }

        if (!profile.isEmpty())
        {
            if (names.isEmpty())
            {
                profiles.erase(profile.toStdString());
            }
            else
            {
                profiles.insert_or_assign(profile.toStdString(), labelsToToml(names));
            }
        }

        if (!profiles.empty())
        {
            updated.insert_or_assign(ProfilesKey, std::move(profiles));
        }

        if (updated.empty())
        {
            table.erase(wheelKey);
        }
        else
        {
            table.insert_or_assign(wheelKey, std::move(updated));
        }

        std::ostringstream stream;
        stream << table << '\n';

        QDir().mkpath(QFileInfo(path).absolutePath());

        if (QSaveFile file(path); !file.open(QIODevice::WriteOnly) ||
            file.write(QByteArray::fromStdString(stream.str())) < 0 || !file.commit())
        {
            return std::unexpected(i18n("Cannot write \"%1\": %2", path, file.errorString()));
        }

        return {};
    }
} // namespace Wheelstar::Settings
