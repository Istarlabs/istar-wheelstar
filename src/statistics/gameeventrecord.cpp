// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "statistics/gameeventrecord.h"

using namespace Qt::StringLiterals;

namespace Wheelstar::Statistics
{
    QString GameEventRecord::kindKey() const
    {
        switch (kind)
        {
            case Kind::Fine:
                return u"fine"_s;
            case Kind::Toll:
                return u"toll"_s;
            case Kind::Ferry:
                return u"ferry"_s;
            case Kind::Train:
                return u"train"_s;
        }

        return {};
    }

    std::optional<GameEventRecord::Kind> GameEventRecord::kindFromKey(const QString& key)
    {
        for (const Kind kind : {Kind::Fine, Kind::Toll, Kind::Ferry, Kind::Train})
        {
            GameEventRecord probe;
            probe.kind = kind;

            if (probe.kindKey() == key)
            {
                return kind;
            }
        }

        return std::nullopt;
    }

    QJsonObject GameEventRecord::toJson() const
    {
        return {
            {u"record"_s, u"event"_s},
            {u"game"_s, static_cast<int>(game)},
            {u"kind"_s, kindKey()},
            {u"occurredAt"_s, occurredAt.toString(Qt::ISODateWithMs)},
            {u"gameTime"_s, static_cast<qint64>(gameTime)},
            {u"detail"_s, detail},
            {u"from"_s, from},
            {u"to"_s, to},
            {u"amount"_s, amount},
        };
    }

    std::optional<GameEventRecord> GameEventRecord::fromJson(const QJsonObject& object)
    {
        const auto kind = kindFromKey(object.value(u"kind"_s).toString());

        if (object.value(u"record"_s).toString() != "event"_L1 || !kind)
        {
            return std::nullopt;
        }

        GameEventRecord event;
        event.game = static_cast<Telemetry::Game>(object.value(u"game"_s).toInt());
        event.kind = *kind;
        event.occurredAt = QDateTime::fromString(object.value(u"occurredAt"_s).toString(), Qt::ISODateWithMs);
        event.gameTime = static_cast<quint32>(object.value(u"gameTime"_s).toInteger());
        event.detail = object.value(u"detail"_s).toString();
        event.from = object.value(u"from"_s).toString();
        event.to = object.value(u"to"_s).toString();
        event.amount = object.value(u"amount"_s).toInteger();
        return event;
    }
} // namespace Wheelstar::Statistics
