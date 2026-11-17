// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gamedata/hashfsarchive.h"
#include "gamedata/cityhash.h"

#include <QFile>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace Wheelstar::GameData
{
    namespace
    {
        constexpr qint64 HeaderSize = 52;
        constexpr qsizetype EntrySize = 16;
        constexpr qsizetype DescriptorSize = 4;
        constexpr qsizetype DataHeaderSize = 16;
        constexpr quint16 DirectoryFlag = 0x1;
        constexpr quint8 DataPart = 0x80;
        constexpr quint8 ZlibCompressed = 0x10;

        /// zlib data whose inflated size is known; qUncompress wants that size in front, big-endian.
        std::optional<QByteArray> inflate(const QByteArray& deflated, quint32 size)
        {
            QByteArray framed(4, Qt::Uninitialized);
            qToBigEndian(size, framed.data());
            framed.append(deflated);
            QByteArray inflated = qUncompress(framed);

            if (inflated.size() != static_cast<qsizetype>(size))
            {
                return std::nullopt;
            }

            return inflated;
        }

        /// A 24-bit number stored as 16 bits and 8 more.
        quint32 threeBytes(const char* data)
        {
            return qFromLittleEndian<quint16>(data) | (static_cast<quint32>(static_cast<quint8>(data[2])) << 16);
        }

        /// The archive's paths have no leading slash: "def/city.sii", the root is "".
        QByteArray key(const QString& path)
        {
            QString normalised = path;

            while (normalised.startsWith(u'/'))
            {
                normalised.remove(0, 1);
            }

            return normalised.toUtf8();
        }
    } // namespace

    std::expected<HashFsArchive, QString> HashFsArchive::open(const QString& path)
    {
        HashFsArchive archive;
        archive.m_path = path;
        return parse(std::move(archive));
    }

    std::expected<HashFsArchive, QString> HashFsArchive::fromData(QByteArray data)
    {
        HashFsArchive archive;
        archive.m_data = std::move(data);
        return parse(std::move(archive));
    }

    std::expected<HashFsArchive, QString> HashFsArchive::parse(HashFsArchive archive)
    {
        const QByteArray header = archive.bytes(0, HeaderSize);

        if (header.size() != HeaderSize || !header.startsWith("SCS#"))
        {
            return std::unexpected(u"not an SCS archive"_s);
        }

        const char* h = header.constData();

        if (qFromLittleEndian<quint16>(h + 4) != 2 || header.mid(8, 4) != "CITY")
        {
            return std::unexpected(u"not HashFS version 2 with CityHash"_s);
        }

        const auto entryCount = qFromLittleEndian<quint32>(h + 12);
        const auto entriesSize = qFromLittleEndian<quint32>(h + 16);
        const auto descriptorWords = qFromLittleEndian<quint32>(h + 20);
        const auto descriptorsSize = qFromLittleEndian<quint32>(h + 24);
        const auto entriesStart = qFromLittleEndian<quint64>(h + 28);
        const auto descriptorsStart = qFromLittleEndian<quint64>(h + 36);

        const auto entries = inflate(archive.bytes(static_cast<qint64>(entriesStart), entriesSize),
                                     entryCount * static_cast<quint32>(EntrySize));
        const auto descriptors = inflate(archive.bytes(static_cast<qint64>(descriptorsStart), descriptorsSize),
                                         descriptorWords * static_cast<quint32>(DescriptorSize));

        if (!entries || !descriptors)
        {
            return std::unexpected(u"the index cannot be unpacked"_s);
        }

        archive.m_descriptors = *descriptors;
        archive.m_entries.reserve(entryCount);

        for (quint32 i = 0; i < entryCount; ++i)
        {
            const char* entry = entries->constData() + (static_cast<qsizetype>(i) * EntrySize);
            archive.m_entries.insert(qFromLittleEndian<quint64>(entry),
                                     {.descriptors = qFromLittleEndian<quint32>(entry + 8),
                                      .parts = qFromLittleEndian<quint16>(entry + 12),
                                      .directory = (qFromLittleEndian<quint16>(entry + 14) & DirectoryFlag) != 0});
        }

        return archive;
    }

    QByteArray HashFsArchive::bytes(qint64 offset, qint64 size) const
    {
        if (m_path.isEmpty())
        {
            return m_data.mid(offset, size);
        }

        // A file per read: an archive is opened once, read a few times, and the files can be many gigabytes.
        QFile file(m_path);

        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset))
        {
            return {};
        }

        return file.read(size);
    }

    std::optional<QByteArray> HashFsArchive::content(const Entry& entry) const
    {
        // An entry has a few parts; the one with the data says where it is and how it is packed. The others
        // (texture headers and the like) do not matter here.
        for (quint32 part = 0; part < entry.parts; ++part)
        {
            const qsizetype at = static_cast<qsizetype>(entry.descriptors + part) * DescriptorSize;

            if (at + DescriptorSize > m_descriptors.size())
            {
                return std::nullopt;
            }

            const char* descriptor = m_descriptors.constData() + at;

            if ((static_cast<quint8>(descriptor[3]) & DataPart) == 0)
            {
                continue;
            }

            const qsizetype dataAt = static_cast<qsizetype>(threeBytes(descriptor)) * DescriptorSize;

            if (dataAt + DataHeaderSize > m_descriptors.size())
            {
                return std::nullopt;
            }

            const char* data = m_descriptors.constData() + dataAt;
            const quint32 storedSize = threeBytes(data);
            const auto compression = static_cast<quint8>(static_cast<quint8>(data[3]) & 0xF0);
            const quint32 size = threeBytes(data + 4);
            // In units of 16 bytes.
            const qint64 offset = static_cast<qint64>(qFromLittleEndian<quint32>(data + 12)) * 16;
            QByteArray stored = bytes(offset, storedSize);

            if (stored.size() != static_cast<qsizetype>(storedSize))
            {
                return std::nullopt;
            }

            if (compression == ZlibCompressed)
            {
                return inflate(stored, size);
            }

            // ponytail: other packings (GDeflate, for textures) are passed through; nothing read here uses them.
            return stored;
        }

        return std::nullopt;
    }

    std::optional<QByteArray> HashFsArchive::read(const QString& path) const
    {
        const auto entry = m_entries.constFind(cityHash64(key(path)));

        if (entry == m_entries.cend() || entry->directory)
        {
            return std::nullopt;
        }

        return content(*entry);
    }

    std::optional<QStringList> HashFsArchive::list(const QString& path) const
    {
        const auto entry = m_entries.constFind(cityHash64(key(path)));

        if (entry == m_entries.cend() || !entry->directory)
        {
            return std::nullopt;
        }

        const auto data = content(*entry);

        if (!data || data->size() < 4)
        {
            return std::nullopt;
        }

        // A count, a byte with the length of each name, then the names back to back.
        const auto count = qFromLittleEndian<quint32>(data->constData());
        qsizetype at = 4 + static_cast<qsizetype>(count);

        if (at > data->size())
        {
            return std::nullopt;
        }

        QStringList names;

        for (quint32 i = 0; i < count; ++i)
        {
            const auto length = static_cast<quint8>(data->at(4 + static_cast<qsizetype>(i)));

            if (at + length > data->size())
            {
                return std::nullopt;
            }

            names.append(QString::fromUtf8(data->sliced(at, length)));
            at += length;
        }

        return names;
    }
} // namespace Wheelstar::GameData
