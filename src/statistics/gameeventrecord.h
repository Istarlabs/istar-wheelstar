// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plugin/telemetry_packet.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace Wheelstar::Statistics
{
    /// Something that cost money on the road: a fine, a toll, a ferry, a train.
    struct GameEventRecord
    {
        enum class Kind
        {
            Fine,
            Toll,
            Ferry,
            Train,
        };

        Telemetry::Game game = Telemetry::Game::Unknown;
        Kind kind = Kind::Fine;
        QDateTime occurredAt;
        /// Packet::gameTime when it happened.
        quint32 gameTime = 0;
        /// The offence of a fine, as the SDK names it ("speeding", "red_signal" ...).
        QString detail;
        /// Where a ferry or a train went from and to.
        QString from;
        QString to;
        /// In the game's currency.
        qint64 amount = 0;

        /// The kind as the database keeps it: "fine", "toll", "ferry", "train".
        [[nodiscard]] QString kindKey() const;
        static std::optional<Kind> kindFromKey(const QString& key);

        [[nodiscard]] QJsonObject toJson() const;
        static std::optional<GameEventRecord> fromJson(const QJsonObject& object);
    };
} // namespace Wheelstar::Statistics
