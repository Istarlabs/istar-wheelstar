// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/savegamelocator.h"
#include "savegame/savegamefile.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <optional>

using namespace Qt::StringLiterals;

namespace Wheelstar::SaveGame
{
    namespace
    {
        struct GameFolders
        {
            Telemetry::Game game;
            QString appId;
            QString documents;
        };

        const std::array<GameFolders, 2> Games = {{
            {.game = Telemetry::Game::EuroTruckSimulator2,
             .appId = u"227300"_s,
             .documents = u"Euro Truck Simulator 2"_s},
            {.game = Telemetry::Game::AmericanTruckSimulator,
             .appId = u"270880"_s,
             .documents = u"American Truck Simulator"_s},
        }};

        /// The Steam libraries a Steam folder knows: itself and what its libraryfolders.vdf lists.
        QStringList libraries(const QString& steamRoot)
        {
            QStringList result{steamRoot};

            if (QFile vdf(steamRoot + "/steamapps/libraryfolders.vdf"_L1); vdf.open(QIODevice::ReadOnly))
            {
                static const QRegularExpression Path(uR"x("path"\s+"([^"]+)")x"_s);
                auto matches = Path.globalMatch(QString::fromUtf8(vdf.readAll()));

                while (matches.hasNext())
                {
                    result.append(matches.next().captured(1));
                }
            }

            return result;
        }

        /// The profile's name from its profile.sii, if it can be read.
        QString profileName(const QString& directory)
        {
            QFile file(directory + "/profile.sii"_L1);

            if (!file.open(QIODevice::ReadOnly))
            {
                return {};
            }

            const auto document = readSiiFile(file.readAll());
            const SiiUnit* profile = document ? document->first(u"user_profile"_s) : nullptr;
            return profile != nullptr ? profile->text(u"profile_name"_s) : QString();
        }

        /// The profile in this folder with its newest save, or nothing without a save.
        std::optional<SaveGameProfile> profileIn(const QString& directory, Telemetry::Game game)
        {
            SaveGameProfile profile;
            profile.game = game;
            profile.directory = directory;
            QDirIterator saves(directory + "/save"_L1, {u"game.sii"_s}, QDir::Files, QDirIterator::Subdirectories);

            while (saves.hasNext())
            {
                if (const QFileInfo save(saves.next()); save.lastModified() > profile.savedAt)
                {
                    profile.savedAt = save.lastModified();
                    profile.newestSave = save.absoluteFilePath();
                }
            }

            if (profile.newestSave.isEmpty())
            {
                return std::nullopt;
            }

            profile.name = profileName(directory);

            if (profile.name.isEmpty())
            {
                // The folder is the profile's name in hex: "4B75726F6B797575".
                profile.name = QString::fromUtf8(QByteArray::fromHex(QDir(directory).dirName().toLatin1()));
            }

            return profile;
        }
    } // namespace

    QList<SaveGameProfile> findProfiles(const QStringList& steamRoots)
    {
        QStringList candidates;
        QList<Telemetry::Game> games;

        for (const QString& root : steamRoots)
        {
            for (const GameFolders& folders : Games)
            {
                // The Steam Cloud's copy, per Steam account.
                for (const QFileInfo& account :
                     QDir(root + "/userdata"_L1).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                {
                    const QString profiles = account.absoluteFilePath() + u'/' + folders.appId + "/remote/profiles"_L1;

                    for (const QFileInfo& profile : QDir(profiles).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                    {
                        candidates.append(profile.absoluteFilePath());
                        games.append(folders.game);
                    }
                }

                // Profiles kept out of the cloud, in the Proton prefix of every library.
                for (const QString& library : libraries(root))
                {
                    const QString documents = library + "/steamapps/compatdata/"_L1 + folders.appId +
                        "/pfx/drive_c/users/steamuser/Documents/"_L1 + folders.documents;

                    for (const QString& kind : {u"profiles"_s, u"steam_profiles"_s})
                    {
                        for (const QFileInfo& profile :
                             QDir(documents + u'/' + kind).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                        {
                            candidates.append(profile.absoluteFilePath());
                            games.append(folders.game);
                        }
                    }
                }
            }
        }

        QList<SaveGameProfile> result;
        QSet<QString> seen;

        for (qsizetype i = 0; i < candidates.size(); ++i)
        {
            // ~/.steam/steam is a link to ~/.local/share/Steam: the same folder by two names.
            const QString canonical = QFileInfo(candidates.at(i)).canonicalFilePath();

            if (canonical.isEmpty() || seen.contains(canonical))
            {
                continue;
            }

            seen.insert(canonical);

            if (auto profile = profileIn(canonical, games.at(i)))
            {
                result.append(*profile);
            }
        }

        std::ranges::sort(result,
                          [](const SaveGameProfile& a, const SaveGameProfile& b) { return a.savedAt > b.savedAt; });
        return result;
    }

    QStringList steamRoots()
    {
        const QString home = QDir::homePath();
        return {home + "/.local/share/Steam"_L1, home + "/.steam/steam"_L1,
                home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam"_L1};
    }

    QStringList steamLibraries(const QStringList& steamRoots)
    {
        QStringList result;

        for (const QString& root : steamRoots)
        {
            for (const QString& library : libraries(root))
            {
                if (const QString canonical = QFileInfo(library).canonicalFilePath();
                    !canonical.isEmpty() && !result.contains(canonical))
                {
                    result.append(canonical);
                }
            }
        }

        return result;
    }

    QList<GameInstall> findGames(const QStringList& steamRoots)
    {
        QList<GameInstall> result;

        for (const QString& library : steamLibraries(steamRoots))
        {
            for (const GameFolders& folders : Games)
            {
                if (const QString directory = library + "/steamapps/common/"_L1 + folders.documents;
                    QFileInfo::exists(directory + "/def.scs"_L1))
                {
                    result.append({.game = folders.game, .directory = directory});
                }
            }
        }

        return result;
    }

    QList<SaveGameProfile> findProfiles()
    {
        return findProfiles(steamRoots());
    }
} // namespace Wheelstar::SaveGame
