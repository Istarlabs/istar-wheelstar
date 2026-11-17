// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gamedata/hashfsarchive.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace Wheelstar::GameData
{
    /// A thing of the game with its names: a city, a country, a company or a cargo.
    struct GameName
    {
        /// "city", "country", "company" or "cargo".
        QString kind;
        /// The game's id: "wroclaw", "germany", "polarislines", "sandwch_pnls".
        QString id;
        /// The name by the game's language code ("de_de": "Breslau", "pl_pl": "Wrocław", …), and under "native"
        /// the name the definition itself gives, which is there for every kind but the cargo.
        QJsonObject translations;

        friend bool operator==(const GameName&, const GameName&) = default;
    };

    /// The names in these archives, the base game's and its DLCs': the definitions in def/<kind>/<id>.sui and the
    /// translations in locale/<language>/localization.sui. Sorted by kind and id.
    QList<GameName> namesFrom(const QList<HashFsArchive>& archives);

    /// The names of the game installed in this folder, from every .scs archive in it. An archive that cannot be read
    /// is left out.
    QList<GameName> readGameNames(const QString& gameDirectory);

    /// What the game's archives are now, as a short text: their names, sizes and times. It changes with an update of
    /// the game or a DLC, which is when the names are read again.
    QString archivesFingerprint(const QString& gameDirectory);
} // namespace Wheelstar::GameData
