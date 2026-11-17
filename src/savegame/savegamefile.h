// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "savegame/siiunit.h"

#include <QByteArray>
#include <QString>

#include <expected>

namespace Wheelstar::SaveGame
{
    /// The SII files of Euro Truck Simulator 2 and American Truck Simulator come in three shapes:
    ///
    /// - "ScsC": encrypted with AES-256-CBC under a key every copy of the games shares (the one the community's
    ///   SII_Decrypt uses), then zlib-compressed. Inside is one of the two below.
    /// - "BSII": binary. Blocks of structure definitions (a type and its fields with their value types), each
    ///   followed by data blocks that fill one in. Read as the games 1.62 write it, version 3.
    /// - "SiiN": text, which the games write with `g_save_format "2"` in config.cfg.
    ///
    /// Reads any of them into its units, or says why it cannot. docs/save-game-format.md describes the format byte
    /// by byte, and what is read from which unit.
    std::expected<SiiDocument, QString> readSiiFile(const QByteArray& file);

    /// The steps of readSiiFile, for the tests.
    std::expected<QByteArray, QString> decrypt(const QByteArray& file);
    std::expected<SiiDocument, QString> parseBinary(const QByteArray& data);
    std::expected<SiiDocument, QString> parseText(const QByteArray& data);
} // namespace Wheelstar::SaveGame
