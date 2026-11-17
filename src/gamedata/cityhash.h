// SPDX-FileCopyrightText: 2011 Google, Inc.
// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: MIT

#pragma once

#include <QByteArrayView>

namespace Wheelstar::GameData
{
    /// CityHash64 as of CityHash 1.0.3, which the SCS archives look their files up by. Version 1.1 changed the
    /// hash of strings up to 64 bytes, so a current CityHash finds almost nothing in them.
    quint64 cityHash64(QByteArrayView data);
} // namespace Wheelstar::GameData
