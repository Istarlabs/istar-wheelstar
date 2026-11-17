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

namespace Wheelstar::Settings
{
    ButtonLabels loadButtonNames(const QString& path, const QString& wheel)
    {
        if (!QFile::exists(path))
        {
            return {};
        }

        try
        {
            const toml::table table = toml::parse_file(path.toStdString());
            // toml++'s operator[] is a lookup: a missing key gives an empty view.
            const auto* const names =
                table[wheel.toStdString()] // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
                    .as_table();
            return names != nullptr ? labelsFromToml(*names) : ButtonLabels();
        }
        catch (const toml::parse_error&)
        {
            return {};
        }
    }

    std::expected<void, QString> saveButtonNames(const QString& path, const QString& wheel, const ButtonLabels& names)
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

        if (names.isEmpty())
        {
            table.erase(wheel.toStdString());
        }
        else
        {
            table.insert_or_assign(wheel.toStdString(), labelsToToml(names));
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
