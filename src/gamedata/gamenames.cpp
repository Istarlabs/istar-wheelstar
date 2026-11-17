// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gamedata/gamenames.h"
#include "core/logging.h"
#include "savegame/savegamefile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QMap>

#include <array>

using namespace Qt::StringLiterals;

namespace Wheelstar::GameData
{
    namespace
    {
        /// Where each kind is defined, which unit type it is, the prefix of the unit's id before the game's id,
        /// and which fields hold its own name and the key of its translation.
        struct Kind
        {
            QString name;
            QString directory;
            QString unitType;
            QString idPrefix;
            QString nativeField;
            QString localizedField;
        };

        const std::array<Kind, 4> Kinds = {{
            {.name = u"city"_s,
             .directory = u"def/city"_s,
             .unitType = u"city_data"_s,
             .idPrefix = u"city."_s,
             .nativeField = u"city_name"_s,
             .localizedField = u"city_name_localized"_s},
            {.name = u"country"_s,
             .directory = u"def/country"_s,
             .unitType = u"country_data"_s,
             .idPrefix = u"country.data."_s,
             .nativeField = u"name"_s,
             .localizedField = u"name_localized"_s},
            // A company's name is not translated; a cargo's is only a key ("@@cn_glass@@").
            {.name = u"company"_s,
             .directory = u"def/company"_s,
             .unitType = u"company_permanent"_s,
             .idPrefix = u"company.permanent."_s,
             .nativeField = u"name"_s,
             .localizedField = u"name"_s},
            {.name = u"cargo"_s,
             .directory = u"def/cargo"_s,
             .unitType = u"cargo_data"_s,
             .idPrefix = u"cargo."_s,
             .nativeField = u"name"_s,
             .localizedField = u"name"_s},
        }};

        /// "@@berlin@@" is the key "berlin"; anything else is no key.
        QString localizationKey(const QString& value)
        {
            return value.size() > 4 && value.startsWith("@@"_L1) && value.endsWith("@@"_L1)
                ? value.mid(2, value.size() - 4)
                : QString();
        }

        /// The units of a definition file. The files under def/<kind>/ are fragments that def/<kind>.sii includes,
        /// without the "SiiNunit { }" around them; they get it here.
        QList<SaveGame::SiiUnit> unitsOf(const QByteArray& file, const QByteArray& wrapperStart = {},
                                         const QByteArray& wrapperEnd = {})
        {
            auto text = SaveGame::decrypt(file);

            if (!text)
            {
                return {};
            }

            QByteArray data = std::move(*text);

            if (data.startsWith("\xEF\xBB\xBF"))
            {
                data.remove(0, 3);
            }

            if (!data.startsWith("SiiNunit"))
            {
                data = "SiiNunit\n{\n" + wrapperStart + data + "\n" + wrapperEnd + "}\n";
            }

            const auto document = SaveGame::parseText(data);
            QList<SaveGame::SiiUnit> units;

            if (document)
            {
                for (const QString& type :
                     {u"city_data"_s, u"country_data"_s, u"company_permanent"_s, u"cargo_data"_s, u"localization_db"_s})
                {
                    for (const SaveGame::SiiUnit* unit : document->ofType(type))
                    {
                        units.append(*unit);
                    }
                }
            }

            return units;
        }

        /// The files directly in a directory of the archive, with their paths.
        QStringList filesIn(const HashFsArchive& archive, const QString& directory)
        {
            QStringList files;

            for (const QString& name : archive.list(directory).value_or(QStringList()))
            {
                if (!name.startsWith(u'/') && (name.endsWith(".sui"_L1) || name.endsWith(".sii"_L1)))
                {
                    files.append(directory + u'/' + name);
                }
            }

            return files;
        }

        struct Definition
        {
            QString native;
            QString key;
        };
    } // namespace

    QList<GameName> namesFrom(const QList<HashFsArchive>& archives)
    {
        // By kind, then id; a DLC that defines a unit again wins, as in the game, which loads it later.
        QMap<std::pair<QString, QString>, Definition> definitions;
        // By language, then key.
        QHash<QString, QHash<QString, QString>> translations;

        for (const HashFsArchive& archive : archives)
        {
            for (const Kind& kind : Kinds)
            {
                for (const QString& path : filesIn(archive, kind.directory))
                {
                    for (const SaveGame::SiiUnit& unit : unitsOf(archive.read(path).value_or(QByteArray())))
                    {
                        if (unit.type != kind.unitType || !unit.id.startsWith(kind.idPrefix))
                        {
                            continue;
                        }

                        const QString native = unit.text(kind.nativeField);
                        const QString key = localizationKey(unit.text(kind.localizedField));
                        definitions.insert(
                            {kind.name, unit.id.mid(kind.idPrefix.size())},
                            {.native = localizationKey(native).isEmpty() ? native : QString(), .key = key});
                    }
                }
            }

            // The languages are the folders under locale/: "/de_de", "/pl_pl", …
            for (const QString& folder : archive.list(u"locale"_s).value_or(QStringList()))
            {
                if (!folder.startsWith(u'/'))
                {
                    continue;
                }

                const QString language = folder.mid(1);
                // The file is the inside of a localization_db unit, which local.sii includes.
                const auto units =
                    unitsOf(archive.read(u"locale/"_s + language + u"/localization.sui"_s).value_or(QByteArray()),
                            "localization_db : .localization\n{\n", "}\n");

                for (const SaveGame::SiiUnit& unit : units)
                {
                    const QStringList keys = unit.texts(u"key"_s);
                    const QStringList values = unit.texts(u"val"_s);
                    QHash<QString, QString>& names = translations[language];

                    for (qsizetype i = 0; i < keys.size() && i < values.size(); ++i)
                    {
                        names.insert(keys.at(i), values.at(i));
                    }
                }
            }
        }

        QList<GameName> result;

        for (auto it = definitions.cbegin(); it != definitions.cend(); ++it)
        {
            GameName name{.kind = it.key().first, .id = it.key().second, .translations = {}};

            if (!it->native.isEmpty())
            {
                name.translations.insert(u"native"_s, it->native);
            }

            if (!it->key.isEmpty())
            {
                for (auto language = translations.cbegin(); language != translations.cend(); ++language)
                {
                    if (const auto value = language->constFind(it->key); value != language->cend())
                    {
                        name.translations.insert(language.key(), *value);
                    }
                }
            }

            if (!name.translations.isEmpty())
            {
                result.append(name);
            }
        }

        return result;
    }

    QList<GameName> readGameNames(const QString& gameDirectory)
    {
        QList<HashFsArchive> archives;
        // The base game first, then its DLCs in the order of their names, which is how they are loaded.
        QStringList files = QDir(gameDirectory).entryList({u"*.scs"_s}, QDir::Files, QDir::Name);
        std::ranges::stable_partition(files, [](const QString& file) { return !file.startsWith("dlc_"_L1); });

        for (const QString& file : files)
        {
            auto archive = HashFsArchive::open(QDir(gameDirectory).filePath(file));

            if (!archive)
            {
                qCDebug(WHEELSTAR) << "Not reading" << file << archive.error();
                continue;
            }

            archives.append(std::move(*archive));
        }

        return namesFrom(archives);
    }

    QString archivesFingerprint(const QString& gameDirectory)
    {
        QCryptographicHash hash(QCryptographicHash::Sha1);

        for (const QFileInfo& file : QDir(gameDirectory).entryInfoList({u"*.scs"_s}, QDir::Files, QDir::Name))
        {
            hash.addData(file.fileName().toUtf8());
            hash.addData(QByteArray::number(file.size()));
            hash.addData(QByteArray::number(file.lastModified().toMSecsSinceEpoch()));
        }

        return QString::fromLatin1(hash.result().toHex());
    }
} // namespace Wheelstar::GameData
