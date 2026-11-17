// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/career.h"
#include "savegame/savegamefile.h"
#include "savegame/savegamelocator.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <openssl/evp.h>

#include <array>

using namespace Wheelstar;
using namespace Wheelstar::SaveGame;

namespace
{
    /// A save in the text format, with what the career and the delivery log are read from.
    const QByteArray TextSave = R"(SiiNunit
{
bank : _nameless.1 {
 money_account: 9428927
}

economy : _nameless.2 {
 bank: _nameless.1
 experience_points: 314114
 game_time: 406437
 total_distances_by_mode: 3
 total_distances_by_mode[0]: 240255
 total_distances_by_mode[1]: 3849
 total_distances_by_mode[2]: 0
 visited_cities: 2
 visited_cities[0]: hannover
 visited_cities[1]: bremen
 delivery_log: _nameless.3
 bus_job_log: _nameless.7
 police_ctrl: _nameless.5
 total_fuel_litres: 30719
}

bus_job_log : _nameless.7 {
 version: 0
 entries: 1
 entries[0]: _nameless.8
}

bus_job_log_entry : _nameless.8 {
 flags: 1
 type: company_job
 assigned_bus: vehicle.volvo.9700_23
 source_bus_stop_name: city.zurich
 target_bus_stop_name: city.geneve
 stage_count: 2
 exp_delta: 438
 reward: 4515
 travelled_distance: 265
 start_time: 400620
 end_time: 400862
 total_passengers: 84
 satisfaction: &3f400000
}

delivery_log : _nameless.3 {
 entries: 2
 entries[0]: _nameless.4
 entries[1]: _nameless.6
}

delivery_log_entry : _nameless.4 {
 params: 19
 params[0]: "397719"
 params[1]: "company.volatile.polarislines.isafjordur"
 params[2]: "company.volatile.blatt_voruhu.thorshofn"
 params[3]: "cargo.sandwch_pnls"
 params[4]: "1162"
 params[5]: "35851"
 params[6]: "1002"
 params[7]: "0.016"
 params[8]: "1"
 params[9]: "0"
 params[10]: "1"
 params[11]: "1"
 params[12]: "0"
 params[13]: "38386"
 params[14]: "250"
 params[15]: "397719"
 params[16]: "vehicle.volvo.fh_2024"
 params[17]: "982"
 params[18]: "on_compn"
}

delivery_log_entry : _nameless.6 {
 params: 19
 params[0]: "225585"
 params[1]: ""
 params[2]: ""
 params[3]: ""
 params[4]: "50"
 params[5]: "0"
 params[6]: "100"
 params[7]: "0.100"
 params[8]: "0"
 params[9]: "0"
 params[10]: "0"
 params[11]: "1"
 params[12]: "0"
 params[13]: "0"
 params[14]: "0"
 params[15]: "0"
 params[16]: ""
 params[17]: "0"
 params[18]: "freerm"
}

police_ctrl : _nameless.5 {
 offence_counter: 2
 offence_counter[0]: 531
 offence_counter[1]: 34
 offence_timer: &3f800000
}
}
)";

    /// Little-endian bytes of a BSII file, built up the way the game writes it.
    class BinaryWriter
    {
    public:
        template <typename T>
        BinaryWriter& value(T number)
        {
            const T little = qToLittleEndian(number);
            m_data.append(reinterpret_cast<const char*>(&little), sizeof(T));
            return *this;
        }

        BinaryWriter& text(const QByteArray& text)
        {
            value<quint32>(static_cast<quint32>(text.size()));
            m_data.append(text);
            return *this;
        }

        [[nodiscard]] QByteArray data() const
        {
            return m_data;
        }

    private:
        QByteArray m_data;
    };

    /// What the games write: the payload deflated, encrypted with the shared key, behind the ScsC header.
    QByteArray encrypt(const QByteArray& payload)
    {
        constexpr std::array<unsigned char, 32> Key = {
            0x2A, 0x5F, 0xCB, 0x17, 0x91, 0xD2, 0x2F, 0xB6, 0x02, 0x45, 0xB3, 0xD8, 0x36, 0x9E, 0xD0, 0xB2,
            0xC2, 0x73, 0x71, 0x56, 0x3F, 0xBF, 0x1F, 0x3C, 0x9E, 0xDF, 0x6B, 0x11, 0x82, 0x5A, 0x5D, 0x0A,
        };
        const std::array<unsigned char, 16> iv{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        // qCompress puts the size in front, which the file keeps in its header instead.
        const QByteArray deflated = qCompress(payload).mid(4);

        QByteArray cipher(deflated.size() + 32, Qt::Uninitialized);
        int written = 0;
        int last = 0;
        EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(context, EVP_aes_256_cbc(), nullptr, Key.data(), iv.data());
        EVP_EncryptUpdate(context, reinterpret_cast<unsigned char*>(cipher.data()), &written,
                          reinterpret_cast<const unsigned char*>(deflated.constData()),
                          static_cast<int>(deflated.size()));
        EVP_EncryptFinal_ex(context, reinterpret_cast<unsigned char*>(cipher.data()) + written, &last);
        EVP_CIPHER_CTX_free(context);
        cipher.truncate(written + last);

        QByteArray file("ScsC");
        file.append(QByteArray(32, '\0'));
        file.append(reinterpret_cast<const char*>(iv.data()), iv.size());
        BinaryWriter size;
        size.value<quint32>(static_cast<quint32>(payload.size()));
        file.append(size.data());
        file.append(cipher);
        return file;
    }
} // namespace

class SaveGameTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theTextFormatGivesTheCareer()
    {
        const auto document = parseText(TextSave);
        QVERIFY(document.has_value());

        const Career career = careerFrom(*document);

        QCOMPARE(career.experience, 314114);
        QCOMPARE(career.money, 9428927);
        QCOMPARE(career.kilometres.at(0), 240255);
        QCOMPARE(career.kilometres.at(1), 3849);
        QCOMPARE(career.visitedCities, 2);
        // Two entries, one of them free roaming; the bus job counts apart.
        QCOMPARE(career.deliveries, 1);
        QCOMPARE(career.busJobs, 1);
        QCOMPARE(career.fuelLitres, 30719);
        QCOMPARE(career.offencesCommitted.size(), Career::Offences.size());
        QCOMPARE(career.offencesCommitted.at(0), 531);
        QCOMPARE(career.offencesCommitted.at(1), 34);
        QCOMPARE(career.offencesCommitted.at(2), 0);
        // A float in hex: &3f800000 is 1.0.
        QCOMPARE(
            document->first(QStringLiteral("police_ctrl"))->values.value(QStringLiteral("offence_timer")).toDouble(),
            1.0);
    }

    void theDeliveryLogGivesJobs()
    {
        const auto document = parseText(TextSave);
        const QList<Statistics::JobRecord> jobs = deliveriesFrom(*document, Telemetry::Game::EuroTruckSimulator2);

        // The free roaming entry has no source and is left out; the bus job, which ended later, comes last.
        QCOMPARE(jobs.size(), 2);
        const Statistics::JobRecord& job = jobs.first();
        QCOMPARE(job.origin, Statistics::JobRecord::Origin::SaveGame);
        QCOMPARE(job.vehicle, Statistics::JobRecord::Vehicle::Truck);
        QCOMPARE(job.gameTime, 397719U);
        QCOMPARE(job.sourceCityId, QStringLiteral("isafjordur"));
        QCOMPARE(job.sourceCompanyId, QStringLiteral("polarislines"));
        QCOMPARE(job.destinationCityId, QStringLiteral("thorshofn"));
        QCOMPARE(job.cargoId, QStringLiteral("sandwch_pnls"));
        QCOMPARE(job.experience, 1162);
        QCOMPARE(job.revenue, 35851);
        QCOMPARE(job.drivenKilometres, 1002.0);
        QCOMPARE(job.cargoDamage, 0.016);
        QCOMPARE(job.fines, 250);
        QCOMPARE(job.truck, QStringLiteral("vehicle.volvo.fh_2024"));
        QCOMPARE(job.market, QStringLiteral("on_compn"));

        const Statistics::JobRecord& bus = jobs.last();
        QCOMPARE(bus.vehicle, Statistics::JobRecord::Vehicle::Bus);
        QCOMPARE(bus.gameTime, 400862U);
        QCOMPARE(bus.deliveryMinutes, 242U);
        QCOMPARE(bus.sourceCityId, QStringLiteral("zurich"));
        QCOMPARE(bus.destinationCityId, QStringLiteral("geneve"));
        QCOMPARE(bus.truck, QStringLiteral("vehicle.volvo.9700_23"));
        QCOMPARE(bus.market, QStringLiteral("company_job"));
        QCOMPARE(bus.revenue, 4515);
        QCOMPARE(bus.experience, 438);
        QCOMPARE(bus.drivenKilometres, 265.0);
        QCOMPARE(bus.passengers, 84);
        QCOMPARE(bus.stops, 2);
        // &3f400000 is 0.75.
        QCOMPARE(bus.satisfaction, 0.75);
        QVERIFY(bus.delivered);
    }

    void aDeliveryTheTelemetrySawIsNotNew()
    {
        const auto document = parseText(TextSave);
        const QList<Statistics::JobRecord> jobs = deliveriesFrom(*document, Telemetry::Game::EuroTruckSimulator2);

        Statistics::JobRecord seen = jobs.first();
        seen.origin = Statistics::JobRecord::Origin::Telemetry;
        seen.gameTime += 3;
        // Only the bus job is left.
        QCOMPARE(newDeliveries(jobs, {seen}).size(), 1);

        seen.gameTime += 600;
        QCOMPARE(newDeliveries(jobs, {seen}).size(), 2);
    }

    void theBinaryFormat()
    {
        BinaryWriter file;
        file.value<quint32>(0x49495342).value<quint32>(3); // "BSII", version 3
        // The structure "economy" (number 7): a u32, an ordinal with two names, a list of ids, a string.
        file.value<quint32>(0).value<quint8>(1).value<quint32>(7).text("economy");
        file.value<quint32>(0x27).text("experience_points");
        file.value<quint32>(0x37).text("mode").value<quint32>(2).value<quint32>(0).text("truck").value<quint32>(1).text(
            "bus");
        file.value<quint32>(0x3A).text("garages");
        file.value<quint32>(0x01).text("name");
        file.value<quint32>(0);
        // Its data: a nameless id, then the values in the order of the fields.
        file.value<quint32>(7).value<quint8>(0xFF).value<quint64>(0x74dbb118);
        file.value<quint32>(314114);
        file.value<quint32>(1);
        // Two named ids, "garage.hannover" packed as tokens; a token is base 38 of its characters plus one.
        const auto token = [](const QByteArray& name)
        {
            static const QByteArray Characters("0123456789abcdefghijklmnopqrstuvwxyz_");
            quint64 value = 0;

            for (auto it = name.crbegin(); it != name.crend(); ++it)
            {
                value = (value * 38) + static_cast<quint64>(Characters.indexOf(*it)) + 1;
            }

            return value;
        };
        file.value<quint32>(1).value<quint8>(2).value<quint64>(token("garage")).value<quint64>(token("hannover"));
        file.text("Driver");

        const auto document = readSiiFile(encrypt(file.data()));

        QVERIFY2(document.has_value(), qPrintable(document.error_or(QString())));
        const SiiUnit* economy = document->first(QStringLiteral("economy"));
        QVERIFY(economy != nullptr);
        QCOMPARE(economy->id, QStringLiteral("_nameless.74dbb118"));
        QCOMPARE(economy->number(QStringLiteral("experience_points")), 314114);
        QCOMPARE(economy->text(QStringLiteral("mode")), QStringLiteral("bus"));
        QCOMPARE(economy->texts(QStringLiteral("garages")), QStringList{QStringLiteral("garage.hannover")});
        QCOMPARE(economy->text(QStringLiteral("name")), QStringLiteral("Driver"));

        // Cut short, it says so instead of reading past the end.
        QVERIFY(!parseBinary(file.data().chopped(3)).has_value());
    }

    void theProfilesAreFound()
    {
        const QTemporaryDir steam;
        const QString profile = steam.filePath(QStringLiteral("userdata/1/227300/remote/profiles/447269766572"));
        QVERIFY(QDir().mkpath(profile + QStringLiteral("/save/autosave")));
        QFile save(profile + QStringLiteral("/save/autosave/game.sii"));
        QVERIFY(save.open(QIODevice::WriteOnly));
        save.write(TextSave);
        save.close();

        const QList<SaveGameProfile> profiles = findProfiles({steam.path()});

        QCOMPARE(profiles.size(), 1);
        QCOMPARE(profiles.first().game, Telemetry::Game::EuroTruckSimulator2);
        // Without a profile.sii, the folder's hex is the name.
        QCOMPARE(profiles.first().name, QStringLiteral("Driver"));
        QVERIFY(profiles.first().newestSave.endsWith(QStringLiteral("autosave/game.sii")));
    }
};

QTEST_GUILESS_MAIN(SaveGameTest)
#include "savegametest.moc"
