// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gamedata/cityhash.h"
#include "gamedata/gamenames.h"
#include "gamedata/hashfsarchive.h"

#include <QTest>
#include <QtEndian>

#include <map>

using namespace Wheelstar::GameData;
using namespace Qt::StringLiterals;

namespace
{
    template <typename T>
    void put(QByteArray& data, T value)
    {
        const T little = qToLittleEndian(value);
        data.append(reinterpret_cast<const char*>(&little), sizeof(T));
    }

    /// A 24-bit number and a byte of flags, as the descriptors store sizes and offsets.
    void putThreeBytes(QByteArray& data, quint32 value, quint8 flags)
    {
        put<quint16>(data, static_cast<quint16>(value & 0xFFFF));
        put<quint8>(data, static_cast<quint8>(value >> 16));
        put<quint8>(data, flags);
    }

    QByteArray deflate(const QByteArray& data)
    {
        // qCompress puts the size in front, which the archive keeps in the descriptor instead.
        return qCompress(data).mid(4);
    }

    /// A directory listing: a count, a byte per name with its length, the names.
    QByteArray listing(const QStringList& names)
    {
        QByteArray data;
        put<quint32>(data, static_cast<quint32>(names.size()));

        for (const QString& name : names)
        {
            data.append(static_cast<char>(name.toUtf8().size()));
        }

        for (const QString& name : names)
        {
            data.append(name.toUtf8());
        }

        return data;
    }

    /// An archive in HashFS version 2 as the games write it: a header, the data of each entry (zlib, 16-byte
    /// aligned), the entry table and the descriptor table, both zlib.
    QByteArray archive(const std::map<QString, std::pair<QByteArray, bool>>& entries)
    {
        constexpr qsizetype DataStart = 0x60;
        QByteArray data(DataStart, '\0');
        QByteArray table;
        QByteArray descriptors;
        QByteArray dataHeaders;
        const auto count = static_cast<quint32>(entries.size());
        quint32 index = 0;

        for (const auto& [path, entry] : entries)
        {
            const auto& [content, directory] = entry;
            const QByteArray stored = deflate(content);
            const auto offset = static_cast<quint32>(data.size() / 16);
            data.append(stored);
            data.append(QByteArray((16 - (data.size() % 16)) % 16, '\0'));

            put<quint64>(table, cityHash64(path.toUtf8()));
            put<quint32>(table, index);
            put<quint16>(table, 1);
            put<quint16>(table, directory ? 1 : 0);

            // The descriptors come first, one word per entry; the data headers after them, four words each.
            putThreeBytes(descriptors, count + (index * 4), 0x80);
            putThreeBytes(dataHeaders, static_cast<quint32>(stored.size()), 0x10);
            putThreeBytes(dataHeaders, static_cast<quint32>(content.size()), 0);
            put<quint32>(dataHeaders, 0);
            put<quint32>(dataHeaders, offset);
            ++index;
        }

        descriptors.append(dataHeaders);
        const QByteArray packedTable = deflate(table);
        const QByteArray packedDescriptors = deflate(descriptors);
        const auto tableStart = static_cast<quint64>(data.size());
        data.append(packedTable);
        const auto descriptorsStart = static_cast<quint64>(data.size());
        data.append(packedDescriptors);

        QByteArray header("SCS#");
        put<quint16>(header, 2);
        put<quint16>(header, 0);
        header.append("CITY");
        put<quint32>(header, count);
        put<quint32>(header, static_cast<quint32>(packedTable.size()));
        put<quint32>(header, static_cast<quint32>(descriptors.size() / 4));
        put<quint32>(header, static_cast<quint32>(packedDescriptors.size()));
        put<quint64>(header, tableStart);
        put<quint64>(header, descriptorsStart);
        put<quint64>(header, 0);
        data.replace(0, header.size(), header);
        return data;
    }

    /// A base game with one city, country, company and cargo, and two languages.
    QByteArray baseGame()
    {
        return archive({
            {u""_s, {listing({u"/def"_s, u"/locale"_s}), true}},
            {u"def"_s, {listing({u"/city"_s, u"/company"_s, u"/cargo"_s, u"/country"_s}), true}},
            {u"def/city"_s, {listing({u"wroclaw.sui"_s, u"/wroclaw"_s}), true}},
            {u"def/city/wroclaw.sui"_s,
             {"city_data : city.wroclaw\n{\n\tcity_name: \"Wrocław\"\n\tcity_name_localized: \"@@wroclaw@@\"  # the "
              "key\n\tcountry: poland\n}\n",
              false}},
            {u"def/country"_s, {listing({u"poland.sui"_s}), true}},
            {u"def/country/poland.sui"_s,
             {"\xEF\xBB\xBF"
              "country_data : country.data.poland\n{\n\tname: \"Polska\"\n\tname_localized: "
              "\"@@poland@@\"\n}\n",
              false}},
            {u"def/company"_s, {listing({u"polarislines.sui"_s}), true}},
            {u"def/company/polarislines.sui"_s,
             {"company_permanent: company.permanent.polarislines\n{\n\tname: \"Polaris Lines\"\n}\n", false}},
            {u"def/cargo"_s, {listing({u"sandwch_pnls.sui"_s}), true}},
            {u"def/cargo/sandwch_pnls.sui"_s,
             {"cargo_data: cargo.sandwch_pnls\n{\n\tname: \"@@cn_sandwich_panels@@\"\n}\n", false}},
            {u"locale"_s, {listing({u"/de_de"_s, u"/pl_pl"_s}), true}},
            {u"locale/de_de"_s, {listing({u"localization.sui"_s}), true}},
            {u"locale/de_de/localization.sui"_s,
             {"\tkey[]: \"wroclaw\"\n\tval[]: \"Breslau\" # with a comment\n\tkey[]: \"poland\"\n\tval[]: "
              "\"Polen\"\n\tkey[]: \"cn_sandwich_panels\"\n\tval[]: \"Sandwichpaneele\"\n",
              false}},
            {u"locale/pl_pl"_s, {listing({u"localization.sui"_s}), true}},
            {u"locale/pl_pl/localization.sui"_s,
             {"\tkey[]: \"wroclaw\"\n\tval[]: \"Wrocław\"\n\tkey[]: \"poland\"\n\tval[]: \"Polska\"\n", false}},
        });
    }
} // namespace

class GameDataTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cityHashMatchesTheGamesArchives_data()
    {
        QTest::addColumn<QByteArray>("path");
        QTest::addColumn<quint64>("hash");

        // Paths of every length class of the hash, each found by it in the def.scs of ETS2 1.5x.
        QTest::newRow("empty") << QByteArray() << quint64{0x9ae16a3b2f90404fULL};
        QTest::newRow("1-3") << QByteArray("def") << quint64{0x2c6f469efb31c45aULL};
        QTest::newRow("4-8") << QByteArray("custom") << quint64{0xe31afc07be2b38fbULL};
        QTest::newRow("9-16") << QByteArray("custom/city.sii") << quint64{0x1ffe051698fba3e2ULL};
        QTest::newRow("17-32") << QByteArray("custom/country.sii") << quint64{0x2ea90ee918ae99d6ULL};
        QTest::newRow("33-64") << QByteArray("def/academy_rewards_meta_data.sii") << quint64{0x4f6910f773d37139ULL};
        QTest::newRow("65+") << QByteArray("def/sign/board/bg_sr_board_roundabout_white_right_diag_left_v02.sii")
                             << quint64{0xfd1f10d753d37176ULL};
    }

    void cityHashMatchesTheGamesArchives()
    {
        QFETCH(QByteArray, path);
        QFETCH(quint64, hash);

        QCOMPARE(cityHash64(path), hash);
    }

    void anArchiveIsListedAndRead()
    {
        const auto archive = HashFsArchive::fromData(baseGame());

        QVERIFY2(archive.has_value(), qPrintable(archive.error_or(QString())));
        QCOMPARE(archive->list(u""_s), QStringList({u"/def"_s, u"/locale"_s}));
        QCOMPARE(archive->list(u"/def/city"_s), QStringList({u"wroclaw.sui"_s, u"/wroclaw"_s}));
        QVERIFY(archive->read(u"def/company/polarislines.sui"_s)->contains("Polaris Lines"));
        // A directory is not a file, a file not a directory, and what is not there is neither.
        QVERIFY(!archive->read(u"def"_s).has_value());
        QVERIFY(!archive->list(u"def/city/wroclaw.sui"_s).has_value());
        QVERIFY(!archive->read(u"def/nothing.sii"_s).has_value());
    }

    void somethingElseIsNoArchive()
    {
        QVERIFY(!HashFsArchive::fromData("SCS#").has_value());
        QVERIFY(!HashFsArchive::fromData(QByteArray(64, 'x')).has_value());
    }

    void theNamesComeWithTheirTranslations()
    {
        const auto base = HashFsArchive::fromData(baseGame());
        QVERIFY(base.has_value());

        const QList<GameName> names = namesFrom({*base});

        QCOMPARE(names.size(), 4);
        // Sorted by kind, then id.
        QCOMPARE(names.at(0).kind, u"cargo"_s);
        // A cargo has no name of its own, only the key of its translation.
        QCOMPARE(names.at(0).translations, QJsonObject({{u"de_de"_s, u"Sandwichpaneele"_s}}));
        QCOMPARE(names.at(1).kind, u"city"_s);
        QCOMPARE(names.at(1).id, u"wroclaw"_s);
        QCOMPARE(names.at(1).translations,
                 QJsonObject({{u"native"_s, u"Wrocław"_s}, {u"de_de"_s, u"Breslau"_s}, {u"pl_pl"_s, u"Wrocław"_s}}));
        // A company's name is not translated.
        QCOMPARE(names.at(2).id, u"polarislines"_s);
        QCOMPARE(names.at(2).translations, QJsonObject({{u"native"_s, u"Polaris Lines"_s}}));
        QCOMPARE(names.at(3).kind, u"country"_s);
        QCOMPARE(names.at(3).translations.value(u"de_de"_s).toString(), u"Polen"_s);
    }

    void aLaterArchiveDefinesAgain()
    {
        const auto base = HashFsArchive::fromData(baseGame());
        // A DLC that defines the city again, as a map DLC does with a town it reworks.
        const auto dlc = HashFsArchive::fromData(archive({
            {u""_s, {listing({u"/def"_s}), true}},
            {u"def"_s, {listing({u"/city"_s}), true}},
            {u"def/city"_s, {listing({u"wroclaw.sui"_s}), true}},
            {u"def/city/wroclaw.sui"_s,
             {"city_data : city.wroclaw\n{\n\tcity_name: \"Wrocław (DLC)\"\n\tcity_name_localized: "
              "\"@@wroclaw@@\"\n}\n",
              false}},
        }));
        QVERIFY(base.has_value() && dlc.has_value());

        const QList<GameName> names = namesFrom({*base, *dlc});

        QCOMPARE(names.at(1).translations.value(u"native"_s).toString(), u"Wrocław (DLC)"_s);
        QCOMPARE(names.at(1).translations.value(u"de_de"_s).toString(), u"Breslau"_s);
    }
};

QTEST_GUILESS_MAIN(GameDataTest)
#include "gamedatatest.moc"
