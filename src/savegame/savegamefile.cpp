// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/savegamefile.h"

#include <KLocalizedString>
#include <QRegularExpression>
#include <QtEndian>

#include <openssl/evp.h>

#include <array>
#include <bit>
#include <cstring>
#include <memory>

using namespace Qt::StringLiterals;

namespace Wheelstar::SaveGame
{
    namespace
    {
        /// The key of the "ScsC" files. It is the same in every copy of the games, which is why the community's
        /// tools (SII_Decrypt and the save editors) can open anyone's save.
        constexpr std::array<unsigned char, 32> Key = {
            0x2A, 0x5F, 0xCB, 0x17, 0x91, 0xD2, 0x2F, 0xB6, 0x02, 0x45, 0xB3, 0xD8, 0x36, 0x9E, 0xD0, 0xB2,
            0xC2, 0x73, 0x71, 0x56, 0x3F, 0xBF, 0x1F, 0x3C, 0x9E, 0xDF, 0x6B, 0x11, 0x82, 0x5A, 0x5D, 0x0A,
        };

        // The "ScsC" header: the magic (4 bytes), an HMAC the games check and this reader does not (32), the
        // initialisation vector (16), the size of the data once inflated (4); the encrypted data follows.
        constexpr qsizetype IvOffset = 36;
        constexpr qsizetype SizeOffset = 52;
        constexpr qsizetype DataOffset = 56;

        /// Reads the little-endian values of a BSII file front to back, and remembers when it ran out.
        class Reader
        {
        public:
            explicit Reader(const QByteArray& data) : m_data(data)
            {
            }

            [[nodiscard]] bool atEnd() const
            {
                return m_position >= m_data.size();
            }

            [[nodiscard]] bool failed() const
            {
                return m_failed;
            }

            template <typename T>
            T read()
            {
                T value{};

                if (!take(sizeof(T)))
                {
                    return value;
                }

                std::memcpy(&value, m_data.constData() + m_position - static_cast<qsizetype>(sizeof(T)), sizeof(T));
                return qFromLittleEndian(value);
            }

            void skip(qsizetype count)
            {
                take(count);
            }

            QString string()
            {
                const auto length = static_cast<qsizetype>(read<quint32>());

                if (!take(length))
                {
                    return {};
                }

                return QString::fromUtf8(m_data.constData() + m_position - length, length);
            }

            /// A name packed into 64 bits: up to twelve characters of "0-9a-z_", in base 38 with 0 for "none".
            QString token()
            {
                static constexpr std::string_view Characters = "0123456789abcdefghijklmnopqrstuvwxyz_";
                auto value = read<quint64>();
                QString result;

                while (value != 0)
                {
                    --value;
                    result.append(QLatin1Char(Characters.at(value % 38)));
                    value /= 38;
                }

                return result;
            }

            /// A unit's id: its parts as tokens joined by dots, or 0xFF and a number for a nameless one.
            QString id()
            {
                const auto parts = read<quint8>();

                if (parts == 0xFF)
                {
                    return u"_nameless."_s + QString::number(read<quint64>(), 16);
                }

                QStringList names;

                for (quint8 i = 0; i < parts; ++i)
                {
                    names.append(token());
                }

                return names.join(u'.');
            }

        private:
            bool take(qsizetype count)
            {
                if (m_failed || count < 0 || m_position + count > m_data.size())
                {
                    m_failed = true;
                    return false;
                }

                m_position += count;
                return true;
            }

            const QByteArray& m_data;
            qsizetype m_position = 0;
            bool m_failed = false;
        };

        struct Field
        {
            quint32 type = 0;
            QString name;
            /// The names of an ordinal field (type 0x37), by value.
            QHash<quint32, QString> ordinals;
        };

        struct Structure
        {
            QString name;
            QList<Field> fields;
        };

        template <typename T>
        QVariantList readList(Reader& reader)
        {
            QVariantList list;
            const auto count = reader.read<quint32>();

            for (quint32 i = 0; i < count && !reader.failed(); ++i)
            {
                list.append(QVariant::fromValue(reader.read<T>()));
            }

            return list;
        }

        /// One value of a data block. Vectors, quaternions and placements are skipped: nothing here uses them.
        std::expected<QVariant, QString> readValue(Reader& reader, const Field& field)
        {
            // The fixed sizes of the skipped kinds, in bytes, and the size of one element of their lists.
            switch (field.type)
            {
                case 0x01:
                    return reader.string();
                case 0x02:
                {
                    QVariantList list;
                    const auto count = reader.read<quint32>();

                    for (quint32 i = 0; i < count && !reader.failed(); ++i)
                    {
                        list.append(reader.string());
                    }

                    return list;
                }
                case 0x03:
                    return reader.token();
                case 0x04:
                {
                    QVariantList list;
                    const auto count = reader.read<quint32>();

                    for (quint32 i = 0; i < count && !reader.failed(); ++i)
                    {
                        list.append(reader.token());
                    }

                    return list;
                }
                case 0x05:
                    return static_cast<double>(reader.read<float>());
                case 0x06:
                    return readList<float>(reader);
                case 0x07: // two floats
                    reader.skip(8);
                    return QVariant();
                case 0x08:
                    reader.skip(static_cast<qsizetype>(reader.read<quint32>()) * 8);
                    return QVariant();
                case 0x09: // three floats
                case 0x11: // three integers
                    reader.skip(12);
                    return QVariant();
                case 0x0A:
                case 0x12:
                    reader.skip(static_cast<qsizetype>(reader.read<quint32>()) * 12);
                    return QVariant();
                case 0x17: // four floats, a quaternion
                    reader.skip(16);
                    return QVariant();
                case 0x18:
                    reader.skip(static_cast<qsizetype>(reader.read<quint32>()) * 16);
                    return QVariant();
                case 0x19: // a placement: a position of three floats, a padding of four bytes, a quaternion
                    reader.skip(32);
                    return QVariant();
                case 0x1A:
                    reader.skip(static_cast<qsizetype>(reader.read<quint32>()) * 32);
                    return QVariant();
                case 0x25:
                    return static_cast<qint64>(reader.read<qint32>());
                case 0x26:
                    return readList<qint32>(reader);
                case 0x27:
                case 0x2F:
                    return static_cast<qint64>(reader.read<quint32>());
                case 0x28:
                    return readList<quint32>(reader);
                case 0x2B:
                    return static_cast<qint64>(reader.read<quint16>());
                case 0x2C:
                    return readList<quint16>(reader);
                case 0x31:
                    return reader.read<qint64>();
                case 0x32:
                    return readList<qint64>(reader);
                case 0x33:
                    return reader.read<quint64>();
                case 0x34:
                    return readList<quint64>(reader);
                case 0x35:
                    return reader.read<quint8>() != 0;
                case 0x36:
                    return readList<quint8>(reader);
                case 0x37:
                    return field.ordinals.value(reader.read<quint32>());
                case 0x39:
                case 0x3B:
                case 0x3D:
                    return reader.id();
                case 0x3A:
                case 0x3C:
                case 0x3E:
                {
                    QVariantList list;
                    const auto count = reader.read<quint32>();

                    for (quint32 i = 0; i < count && !reader.failed(); ++i)
                    {
                        list.append(reader.id());
                    }

                    return list;
                }
                default:
                    break;
            }

            return std::unexpected(
                i18n("The save game has a value of a kind this program does not know (0x%1, \"%2\").",
                     QString::number(field.type, 16), field.name));
        }

        /// A value of a text SII file: a quoted string, a float written as "&" and its bits in hex, a number, a
        /// bool, or a word (a token, an id, a vector) kept as text.
        QVariant textValue(QStringView raw)
        {
            if (raw.size() >= 2 && raw.startsWith(u'"') && raw.endsWith(u'"'))
            {
                QString text = raw.sliced(1, raw.size() - 2).toString();
                text.replace(u"\\\""_s, u"\""_s).replace(u"\\\\"_s, u"\\"_s);
                return text;
            }

            if (raw.startsWith(u'&'))
            {
                bool valid = false;
                const quint32 bits = raw.sliced(1).toUInt(&valid, 16);
                return valid ? QVariant(static_cast<double>(std::bit_cast<float>(bits))) : QVariant(raw.toString());
            }

            bool valid = false;
            const qint64 integer = raw.toLongLong(&valid);

            if (valid)
            {
                return integer;
            }

            const double real = raw.toDouble(&valid);

            if (valid)
            {
                return real;
            }

            if (raw == u"true" || raw == u"false")
            {
                return raw == u"true";
            }

            return raw == u"nil" ? QString() : raw.toString();
        }

        /// A line without its comment: "#" or "//" up to the end, unless inside quotes.
        QString withoutComment(const QString& line)
        {
            bool quoted = false;

            for (qsizetype i = 0; i < line.size(); ++i)
            {
                const QChar c = line.at(i);

                if (c == u'\\' && quoted)
                {
                    ++i;
                }
                else if (c == u'"')
                {
                    quoted = !quoted;
                }
                else if (!quoted && (c == u'#' || (c == u'/' && line.mid(i + 1, 1) == u"/")))
                {
                    return line.left(i);
                }
            }

            return line;
        }
    } // namespace

    std::expected<QByteArray, QString> decrypt(const QByteArray& file)
    {
        if (!file.startsWith("ScsC"))
        {
            return file;
        }

        if (file.size() <= DataOffset)
        {
            return std::unexpected(i18n("The save game is cut short."));
        }

        const std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(),
                                                                                      &EVP_CIPHER_CTX_free);
        QByteArray plain(file.size() - DataOffset + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
        int written = 0;
        int last = 0;
        const auto* iv = reinterpret_cast<const unsigned char*>(file.constData() + IvOffset);

        // No padding to strip: the zlib stream ends where it ends, and what follows it is ignored.
        const bool decrypted = context &&
            EVP_DecryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, Key.data(), iv) == 1 &&
            EVP_CIPHER_CTX_set_padding(context.get(), 0) == 1 &&
            EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char*>(plain.data()), &written,
                              reinterpret_cast<const unsigned char*>(file.constData() + DataOffset),
                              static_cast<int>(file.size() - DataOffset)) == 1 &&
            EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char*>(plain.data()) + written, &last) == 1;

        if (!decrypted)
        {
            return std::unexpected(i18n("The save game cannot be decrypted."));
        }

        plain.truncate(written + last);

        // qUncompress wants the inflated size in front, big-endian; the header has it little-endian.
        const auto size = qFromLittleEndian<quint32>(file.constData() + SizeOffset);
        QByteArray framed(4, Qt::Uninitialized);
        qToBigEndian(size, framed.data());
        framed.append(plain);
        QByteArray inflated = qUncompress(framed);

        if (inflated.size() != static_cast<qsizetype>(size))
        {
            return std::unexpected(i18n("The save game cannot be unpacked."));
        }

        return inflated;
    }

    std::expected<SiiDocument, QString> parseBinary(const QByteArray& data)
    {
        Reader reader(data);

        if (reader.read<quint32>() != qFromLittleEndian<quint32>("BSII"))
        {
            return std::unexpected(i18n("The save game is not in the binary format."));
        }

        reader.read<quint32>(); // the format's version; 3 for the games of 2026
        QHash<quint32, Structure> structures;
        SiiDocument document;

        while (!reader.atEnd() && !reader.failed())
        {
            const auto block = reader.read<quint32>();

            if (block == 0)
            {
                // A structure definition; one that is not valid is only a marker, with nothing after it.
                if (reader.read<quint8>() == 0)
                {
                    continue;
                }

                const auto number = reader.read<quint32>();
                Structure structure;
                structure.name = reader.string();

                while (!reader.failed())
                {
                    Field field;
                    field.type = reader.read<quint32>();

                    if (field.type == 0)
                    {
                        break;
                    }

                    field.name = reader.string();

                    if (field.type == 0x37)
                    {
                        const auto count = reader.read<quint32>();

                        for (quint32 i = 0; i < count && !reader.failed(); ++i)
                        {
                            const auto value = reader.read<quint32>();
                            field.ordinals.insert(value, reader.string());
                        }
                    }

                    structure.fields.append(field);
                }

                structures.insert(number, structure);
                continue;
            }

            const auto structure = structures.constFind(block);

            if (structure == structures.cend())
            {
                return std::unexpected(i18n("The save game refers to a structure it never defined."));
            }

            SiiUnit unit;
            unit.type = structure->name;
            unit.id = reader.id();

            for (const Field& field : structure->fields)
            {
                auto value = readValue(reader, field);

                if (!value)
                {
                    return std::unexpected(value.error());
                }

                unit.values.insert(field.name, *value);
            }

            document.add(std::move(unit));
        }

        if (reader.failed())
        {
            return std::unexpected(i18n("The save game ends in the middle of a value."));
        }

        return document;
    }

    std::expected<SiiDocument, QString> parseText(const QByteArray& data)
    {
        if (!data.startsWith("SiiNunit"))
        {
            return std::unexpected(i18n("The save game is not in the text format."));
        }

        // A save puts the brace on the unit's line; the games' own definition files put it on the next one.
        static const QRegularExpression UnitStart(uR"(^([\w.]+)\s*:\s*([\w.]+)\s*(\{?)$)"_s);
        static const QRegularExpression Entry(uR"(^(\w+)(?:\[(\d*)\])?\s*:\s*(.*)$)"_s);

        SiiDocument document;
        std::optional<SiiUnit> unit;
        // A unit whose brace is still to come.
        std::optional<SiiUnit> opening;
        // The arrays of the unit being read: "key: 3" gives the count, "key[0]: ..." the items.
        QHash<QString, QVariantList> arrays;

        for (const QByteArray& rawLine : data.split('\n'))
        {
            const QString line = withoutComment(QString::fromUtf8(rawLine)).trimmed();

            if (!unit)
            {
                if (opening && line == u"{")
                {
                    unit = std::move(opening);
                    opening.reset();
                    arrays.clear();
                }
                else if (const auto match = UnitStart.match(line); match.hasMatch())
                {
                    SiiUnit next{.type = match.captured(1), .id = match.captured(2), .values = {}};

                    if (match.capturedLength(3) > 0)
                    {
                        unit = std::move(next);
                        arrays.clear();
                    }
                    else
                    {
                        opening = std::move(next);
                    }
                }

                continue;
            }

            if (line == u"}")
            {
                for (auto it = arrays.cbegin(); it != arrays.cend(); ++it)
                {
                    unit->values.insert(it.key(), *it);
                }

                document.add(std::move(*unit));
                unit.reset();
                continue;
            }

            const auto match = Entry.match(line);

            if (!match.hasMatch())
            {
                continue;
            }

            const QString key = match.captured(1);
            const QVariant value = textValue(match.capturedView(3));

            if (match.capturedLength(2) > 0 || line.contains(u"[]"))
            {
                arrays[key].append(value);
            }
            else if (!arrays.contains(key))
            {
                unit->values.insert(key, value);
            }
        }

        return document;
    }

    std::expected<SiiDocument, QString> readSiiFile(const QByteArray& file)
    {
        const auto data = decrypt(file);

        if (!data)
        {
            return std::unexpected(data.error());
        }

        if (data->startsWith("BSII"))
        {
            return parseBinary(*data);
        }

        if (data->startsWith("SiiNunit"))
        {
            return parseText(*data);
        }

        return std::unexpected(i18n("This is not a save game of Euro Truck Simulator 2 or American Truck Simulator."));
    }
} // namespace Wheelstar::SaveGame
