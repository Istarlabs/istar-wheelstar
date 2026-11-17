// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/saveimport.h"

#include <KLocalizedString>

#include <algorithm>
#include <cstdlib>

namespace Wheelstar::SaveGame
{
    static_assert(static_cast<int>(TruckData::Game::EuroTruckSimulator2) ==
                      static_cast<int>(Telemetry::Game::EuroTruckSimulator2) &&
                  static_cast<int>(TruckData::Game::AmericanTruckSimulator) ==
                      static_cast<int>(Telemetry::Game::AmericanTruckSimulator));

    Telemetry::Game telemetryGame(TruckData::Game game)
    {
        return static_cast<Telemetry::Game>(game);
    }

    QString errorText(const SiiError& error)
    {
        // The library's English messages, word for word: the translations of the reader inside Wheelstar stay valid.
        switch (error.kind)
        {
            case SiiError::Kind::CutShort:
                return i18n("The save game is cut short.");
            case SiiError::Kind::CannotDecrypt:
                return i18n("The save game cannot be decrypted.");
            case SiiError::Kind::CannotUnpack:
                return i18n("The save game cannot be unpacked.");
            case SiiError::Kind::NotBinary:
                return i18n("The save game is not in the binary format.");
            case SiiError::Kind::UndefinedStructure:
                return i18n("The save game refers to a structure it never defined.");
            case SiiError::Kind::EndsInValue:
                return i18n("The save game ends in the middle of a value.");
            case SiiError::Kind::NotText:
                return i18n("The save game is not in the text format.");
            case SiiError::Kind::UnknownValueType:
                return i18n("The save game has a value of a kind this program does not know (0x%1, \"%2\").",
                            QString::number(error.valueType, 16), error.field);
            case SiiError::Kind::NotASaveGame:
                break;
        }

        return i18n("This is not a save game of Euro Truck Simulator 2 or American Truck Simulator.");
    }

    QList<Statistics::JobRecord> deliveriesFrom(const SiiDocument& document, Telemetry::Game game)
    {
        QList<Statistics::JobRecord> records;

        for (const TruckData::SaveGame::Job& job : TruckData::SaveGame::deliveriesFrom(document))
        {
            Statistics::JobRecord record;
            record.game = game;
            record.origin = Statistics::JobRecord::Origin::SaveGame;
            record.vehicle = job.vehicle == TruckData::SaveGame::Job::Vehicle::Bus
                ? Statistics::JobRecord::Vehicle::Bus
                : Statistics::JobRecord::Vehicle::Truck;
            record.gameTime = job.gameTime;
            record.sourceCityId = job.sourceCityId;
            record.sourceCompanyId = job.sourceCompanyId;
            record.destinationCityId = job.destinationCityId;
            record.destinationCompanyId = job.destinationCompanyId;
            record.cargoId = job.cargoId;
            record.market = job.market;
            record.truck = job.truck;
            record.drivenKilometres = job.drivenKilometres;
            record.revenue = job.revenue;
            record.experience = job.experience;
            record.cargoDamage = job.cargoDamage;
            record.deliveryMinutes = job.deliveryMinutes;
            record.fines = job.fines;
            record.passengers = job.passengers;
            record.stops = job.stops;
            record.satisfaction = job.satisfaction;
            records.append(record);
        }

        return records;
    }

    QList<Statistics::JobRecord> newDeliveries(const QList<Statistics::JobRecord>& deliveries,
                                               const QList<Statistics::JobRecord>& known)
    {
        // How far apart in game minutes two records of one delivery may be. Two deliveries of the same cargo
        // between the same companies within an in-game hour do not happen.
        constexpr qint64 Tolerance = 60;
        QList<Statistics::JobRecord> result;

        for (const Statistics::JobRecord& delivery : deliveries)
        {
            const bool seen = std::ranges::any_of(known,
                                                  [&delivery](const Statistics::JobRecord& job)
                                                  {
                                                      return job.game == delivery.game &&
                                                          job.sourceCompanyId == delivery.sourceCompanyId &&
                                                          job.destinationCompanyId == delivery.destinationCompanyId &&
                                                          job.cargoId == delivery.cargoId &&
                                                          std::abs(static_cast<qint64>(job.gameTime) -
                                                                   static_cast<qint64>(delivery.gameTime)) <= Tolerance;
                                                  });

            if (!seen)
            {
                result.append(delivery);
            }
        }

        return result;
    }
} // namespace Wheelstar::SaveGame
