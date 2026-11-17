// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/inifile.h"

#include <KLocalizedString>

#include <QFile>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace Wheelstar::Core::IniFile
{
    std::expected<Entries, QString> read(const QString& path)
    {
        QFile file(path);

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return std::unexpected(i18n("Cannot read \"%1\": %2", path, file.errorString()));
        }

        static const QRegularExpression separatorPattern(u"[=:]"_s);
        Entries entries;
        bool inDefault = false;

        for (const QString& raw : QString::fromUtf8(file.readAll()).split(u'\n'))
        {
            const QString line = raw.trimmed();

            if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u';'))
            {
                continue;
            }

            if (line.startsWith(u'['))
            {
                inDefault = line == "[DEFAULT]"_L1;
                continue;
            }

            // configparser accepts both separators and writes " = ".
            if (const qsizetype separator = line.indexOf(separatorPattern); inDefault && separator > 0)
            {
                entries.append({line.left(separator).trimmed().toLower(), line.mid(separator + 1).trimmed()});
            }
        }

        return entries;
    }
} // namespace Wheelstar::Core::IniFile
