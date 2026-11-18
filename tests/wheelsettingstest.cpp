// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings/buttonnames.h"
#include "settings/wheelsettings.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using Wheelstar::Settings::WheelSettings;

class WheelSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tomlRoundTrip()
    {
        const WheelSettings settings{.mode = u"G923"_s,
                                     .range = 540,
                                     .ffGain = 80,
                                     .ffbLeds = true,
                                     .rangeOverlay = u"auto"_s,
                                     .centerWheel = false};

        const QTemporaryDir directory;
        const QString path = directory.filePath(u"profile.toml"_s);
        QVERIFY(settings.save(path).has_value());

        const auto loaded = WheelSettings::load(path);
        QVERIFY2(loaded.has_value(), loaded.has_value() ? "" : qPrintable(loaded.error()));
        QCOMPARE(*loaded, settings);
    }

    void buttonNotesRoundTrip()
    {
        const WheelSettings settings{.range = 540, .buttonNotes = {{0, u"Horn"_s}, {12, u"Lights"_s}}};

        const QTemporaryDir directory;
        const QString path = directory.filePath(u"profile.toml"_s);
        QVERIFY(settings.save(path).has_value());

        const auto loaded = WheelSettings::load(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(*loaded, settings);
        QCOMPARE(loaded->toVariantMap().value(u"button_notes"_s).toMap().value(u"12"_s), u"Lights"_s);
    }

    void profileWithoutNotesKeepsNone()
    {
        WheelSettings base{.range = 900, .buttonNotes = {{1, u"Old"_s}}};
        base.overrideWith({.range = 270, .buttonNotes = {{2, u"New"_s}}});
        QCOMPARE(base.buttonNotes, (Wheelstar::Settings::ButtonLabels{{2, u"New"_s}}));
    }

    void buttonNamesBelongToTheirWheel()
    {
        const QTemporaryDir directory;
        const QString path = directory.filePath(u"names/button-names.toml"_s);
        const Wheelstar::Settings::ButtonLabels first{{0, u"X"_s}, {4, u"Right paddle"_s}};
        const Wheelstar::Settings::ButtonLabels second{{3, u"Horn"_s}};

        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel A"_s, {}, first).has_value());
        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel B"_s, {}, second).has_value());

        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel A"_s), first);
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel B"_s), second);
        QVERIFY(Wheelstar::Settings::loadButtonNames(path, u"Wheel C"_s).isEmpty());

        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel A"_s, {}, {}).has_value());
        QVERIFY(Wheelstar::Settings::loadButtonNames(path, u"Wheel A"_s).isEmpty());
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel B"_s), second);
    }

    void buttonNamesBelongToTheirProfile()
    {
        const QTemporaryDir directory;
        const QString path = directory.filePath(u"names/button-names.toml"_s);
        const Wheelstar::Settings::ButtonLabels wheelLevel{{0, u"X"_s}};
        const Wheelstar::Settings::ButtonLabels formula{{0, u"DRS"_s}, {1, u"Pit limiter"_s}};

        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel"_s, {}, wheelLevel).has_value());
        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel"_s, u"f1"_s, formula).has_value());

        // A profile with names of its own gets them; any other falls back to the wheel's.
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s, u"f1"_s), formula);
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s, u"ets2"_s), wheelLevel);
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s), wheelLevel);

        // Saving the wheel's names keeps the profile's, and the other way round.
        const Wheelstar::Settings::ButtonLabels changed{{0, u"Y"_s}};
        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel"_s, {}, changed).has_value());
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s, u"f1"_s), formula);
        QVERIFY(Wheelstar::Settings::saveButtonNames(path, u"Wheel"_s, u"f1"_s, {}).has_value());
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s, u"f1"_s), changed);
        QCOMPARE(Wheelstar::Settings::loadButtonNames(path, u"Wheel"_s), changed);
    }

    void readsUpstreamProfiles()
    {
        // As upstream Oversteer writes them, booleans as 0 and 1.
        const QTemporaryDir directory;
        const QString path = directory.filePath(u"This.ini"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[DEFAULT]\nmode = G923\nrange = 900\nffb_leds = 1\nrange_overlay = auto\n"
                   "use_buttons = 0\nunknown = 3\n\n");
        file.close();

        const auto loaded = WheelSettings::load(path);
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->mode, u"G923"_s);
        QCOMPARE(loaded->range, 900);
        QCOMPARE(loaded->ffbLeds, true);
        QCOMPARE(loaded->useButtons, false);
        QCOMPARE(loaded->rangeOverlay, u"auto"_s);
        QVERIFY(!loaded->ffGain.has_value());
    }

    void rejectsBrokenToml()
    {
        const QTemporaryDir directory;
        const QString path = directory.filePath(u"broken.toml"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("range = = 3\n");
        file.close();

        QVERIFY(!WheelSettings::load(path).has_value());
    }

    void overrideTakesOnlySetValues()
    {
        WheelSettings base{.range = 900, .ffGain = 100};
        base.overrideWith({.range = 270});
        QCOMPARE(base.range, 270);
        QCOMPARE(base.ffGain, 100);
    }

    void setValueByKey()
    {
        WheelSettings settings;
        QVERIFY(settings.setValue(u"spring_level"_s, 40));
        QVERIFY(settings.setValue(u"ffb_overlay"_s, true));
        QVERIFY(!settings.setValue(u"nonsense"_s, 1));
        QVERIFY(!settings.setValue(u"range"_s, u"wide"_s));
        QCOMPARE(settings.toVariantMap(), (QVariantMap{{u"spring_level"_s, 40}, {u"ffb_overlay"_s, true}}));
    }
};

QTEST_GUILESS_MAIN(WheelSettingsTest)

#include "wheelsettingstest.moc"
