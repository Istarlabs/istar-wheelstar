// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

namespace Wheelstar::GameData
{
    /// An archive of Euro Truck Simulator 2 or American Truck Simulator (.scs) in the HashFS format, version 2, the
    /// one the games of 1.50 and later write. Only the index is read when it is opened; a file is read when it is
    /// asked for. docs/game-archives.md describes the format.
    class HashFsArchive
    {
    public:
        /// The archive at a path, or why it cannot be read: not an archive, another version, a broken index.
        static std::expected<HashFsArchive, QString> open(const QString& path);

        /// A file's content, inflated; nothing for a path that is not in the archive or is a directory.
        [[nodiscard]] std::optional<QByteArray> read(const QString& path) const;
        /// A directory's entries, subdirectories with a leading "/"; nothing for a path that is not a directory.
        [[nodiscard]] std::optional<QStringList> list(const QString& path) const;

        /// An archive from its bytes in memory, for the tests.
        static std::expected<HashFsArchive, QString> fromData(QByteArray data);

    private:
        struct Entry
        {
            quint32 descriptors = 0;
            quint16 parts = 0;
            bool directory = false;
        };

        /// Whatever an entry is, its bytes.
        [[nodiscard]] std::optional<QByteArray> content(const Entry& entry) const;
        [[nodiscard]] QByteArray bytes(qint64 offset, qint64 size) const;
        static std::expected<HashFsArchive, QString> parse(HashFsArchive archive);

        QString m_path;
        /// Only for an archive made from data.
        QByteArray m_data;
        QHash<quint64, Entry> m_entries;
        QByteArray m_descriptors;
    };
} // namespace Wheelstar::GameData
