// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "savegame/siiunit.h"

namespace Wheelstar::SaveGame
{
    QString SiiUnit::text(const QString& key) const
    {
        return values.value(key).toString();
    }

    qint64 SiiUnit::number(const QString& key) const
    {
        return values.value(key).toLongLong();
    }

    QStringList SiiUnit::texts(const QString& key) const
    {
        QStringList result;

        for (const QVariant& value : values.value(key).toList())
        {
            result.append(value.toString());
        }

        return result;
    }

    QList<qint64> SiiUnit::numbers(const QString& key) const
    {
        QList<qint64> result;

        for (const QVariant& value : values.value(key).toList())
        {
            result.append(value.toLongLong());
        }

        return result;
    }

    void SiiDocument::add(SiiUnit unit)
    {
        m_byId.insert(unit.id, m_units.size());
        m_units.append(std::move(unit));
    }

    const SiiUnit* SiiDocument::unit(const QString& id) const
    {
        const auto found = m_byId.constFind(id);
        return found != m_byId.cend() ? &m_units.at(*found) : nullptr;
    }

    const SiiUnit* SiiDocument::first(const QString& type) const
    {
        for (const SiiUnit& unit : m_units)
        {
            if (unit.type == type)
            {
                return &unit;
            }
        }

        return nullptr;
    }

    QList<const SiiUnit*> SiiDocument::ofType(const QString& type) const
    {
        QList<const SiiUnit*> result;

        for (const SiiUnit& unit : m_units)
        {
            if (unit.type == type)
            {
                result.append(&unit);
            }
        }

        return result;
    }

    qsizetype SiiDocument::size() const
    {
        return m_units.size();
    }
} // namespace Wheelstar::SaveGame
