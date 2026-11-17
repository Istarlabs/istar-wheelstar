// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The career as the newest save game of a profile knows it: totals, records and every offence.
Kirigami.ScrollablePage {
    id: page

    readonly property var career: Wheelstar.career
    readonly property bool loaded: career.profile !== undefined

    function number(value: real): string {
        return Number(value).toLocaleString(Qt.locale(), "f", 0);
    }

    function money(value: real): string {
        return number(value) + " " + (career.currency ?? "");
    }

    title: i18n("Career")

    actions: [
        Kirigami.Action {
            text: i18n("Import Jobs")
            icon.name: "document-import"
            tooltip: i18n("Add the deliveries of the save game to the jobs")
            enabled: page.loaded && (page.career.newDeliveries ?? 0) > 0 && !Wheelstar.sessionsLoading
            onTriggered: Wheelstar.importDeliveries()
        },
        Kirigami.Action {
            text: i18n("Read Again")
            icon.name: "view-refresh"
            enabled: !Wheelstar.careerLoading
            onTriggered: Wheelstar.loadCareer(profiles.currentIndex)
        }
    ]

    Component.onCompleted: {
        // The jobs first, so the count of new deliveries is right.
        Wheelstar.refreshSessions();
        Wheelstar.loadCareer(0);
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true
            visible: Wheelstar.saveGameProfiles.length > 1

            QQC2.Label {
                text: i18n("Profile:")
            }

            QQC2.ComboBox {
                id: profiles

                Layout.fillWidth: true
                model: Wheelstar.saveGameProfiles.map(profile => i18n("%1 (%2)", profile.name, profile.game))
                onActivated: index => Wheelstar.loadCareer(index)
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: Wheelstar.careerError.length > 0
            type: Kirigami.MessageType.Error
            text: Wheelstar.careerError
        }

        QQC2.BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: Wheelstar.careerLoading
            visible: running
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: page.loaded

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                // i18n: %1 the profile, %2 the game
                Kirigami.FormData.label: i18n("%1 in %2", page.career.profile ?? "", page.career.game ?? "")
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Saved:")
                text: page.loaded ? Qt.formatDateTime(page.career.savedAt, Qt.locale(), Locale.ShortFormat) : ""
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Experience:")
                text: i18n("%1 XP", page.number(page.career.experience ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Money:")
                text: page.money(page.career.money ?? 0)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Game days:")
                text: page.number(page.career.gameDays ?? 0)
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Driving")
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("By truck:")
                // i18n: %1 kilometres, %2 hours of real time
                text: i18n("%1 km in %2 h", page.number(page.career.truckKilometres ?? 0), page.number(page.career.truckHours ?? 0))
            }

            QQC2.Label {
                visible: (page.career.busKilometres ?? 0) > 0
                Kirigami.FormData.label: i18n("By bus:")
                text: i18n("%1 km in %2 h", page.number(page.career.busKilometres ?? 0), page.number(page.career.busHours ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Deliveries:")
                // i18n: %1 jobs delivered, %2 jobs given up
                text: i18n("%1 (%2 cancelled)", page.number(page.career.deliveries ?? 0), page.number(page.career.cancelledJobs ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Bus jobs:")
                text: page.number(page.career.busJobs ?? 0)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Cities visited:")
                text: page.number(page.career.visitedCities ?? 0)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Garages:")
                text: page.number(page.career.garages ?? 0)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Ferry crossings:")
                text: page.number(page.career.ferryCrossings ?? 0)
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Fuel bought:")
                // i18n: %1 litres, %2 the money paid for them
                text: i18n("%1 l for %2", page.number(page.career.fuelLitres ?? 0), page.money(page.career.fuelPrice ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Visits:")
                // i18n: %1 stops at fuel stations, %2 at service garages
                text: i18n("%1 at fuel stations, %2 at the service", page.number(page.career.gasStationVisits ?? 0),
                           page.number(page.career.serviceVisits ?? 0))
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Records")
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Without an offence:")
                // i18n: %1 the current stretch in km, %2 the longest
                text: i18n("%1 km now, %2 km at most", page.number(page.career.currentWithoutOffence ?? 0),
                           page.number(page.career.longestWithoutOffence ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Without damage:")
                text: i18n("%1 km now, %2 km at most", page.number(page.career.currentClean ?? 0),
                           page.number(page.career.longestClean ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Without cargo damage:")
                text: i18n("%1 km at most", page.number(page.career.longestWithoutCargoDamage ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Jobs to import:")
                text: page.number(page.career.newDeliveries ?? 0)
            }
        }

        Kirigami.Heading {
            level: 3
            visible: page.loaded
            text: i18n("Offences")
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: page.loaded
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("The game counts every offence it notices, also with fines turned off; only some of them were fined.")
        }

        GridLayout {
            Layout.fillWidth: true
            visible: page.loaded && (page.career.offences ?? []).length > 0
            columns: 4
            columnSpacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: i18n("Offence")
                font.bold: true
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignRight
                text: i18n("Noticed")
                font.bold: true
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignRight
                text: i18n("Fined")
                font.bold: true
            }

            QQC2.Label {
                Layout.alignment: Qt.AlignRight
                text: i18n("Paid")
                font.bold: true
            }

            Repeater {
                // Four cells per offence, row by row, as the grid fills them. Not flatMap: Qt's JavaScript engine has
                // no Array.prototype.flatMap, and the model would silently be undefined.
                model: (page.career.offences ?? []).reduce((cells, offence) => cells.concat([offence.name,
                    page.number(offence.committed), page.number(offence.fined), page.money(offence.paid)]), [])

                delegate: QQC2.Label {
                    required property var modelData
                    required property int index

                    Layout.fillWidth: index % 4 === 0
                    Layout.alignment: index % 4 === 0 ? Qt.AlignLeft : Qt.AlignRight
                    text: modelData
                    font.features: { "tnum": 1 }
                }
            }
        }
    }
}
