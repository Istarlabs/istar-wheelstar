// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The player's company as the newest save game knows it: garages, drivers, vehicles and the money. It is read with
/// the career, from the same save.
Kirigami.ScrollablePage {
    id: page

    readonly property var company: Wheelstar.company
    readonly property bool loaded: company.garages !== undefined
    /// "garages", "drivers", "vehicles" or "finances".
    property string view: "garages"

    function number(value: real): string {
        return Number(value).toLocaleString(Qt.locale(), "f", 0);
    }

    function money(value: real): string {
        return number(value) + " " + (company.currency ?? "");
    }

    function percent(share: real): string {
        // i18n: %1 a share in percent
        return i18n("%1 %", Math.round(share * 100));
    }

    title: i18n("Company")

    actions: [
        Kirigami.Action {
            text: i18n("Read Again")
            icon.name: "view-refresh"
            enabled: !Wheelstar.careerLoading
            onTriggered: Wheelstar.loadCareer(0)
        }
    ]

    // The career page reads the save game; coming here first reads it too.
    Component.onCompleted: {
        if (!page.loaded && !Wheelstar.careerLoading) {
            Wheelstar.loadCareer(0);
        }
    }

    header: QQC2.Control {
        padding: Kirigami.Units.largeSpacing
        visible: page.loaded

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                // i18n: %1 garages, %2 drivers, %3 trucks, %4 trailers; the city of the headquarters follows
                text: i18n("%1 garages · %2 drivers · %3 trucks · %4 trailers", page.company.garages?.length ?? 0,
                           page.company.drivers?.length ?? 0, page.company.truckCount ?? 0,
                           page.company.trailerCount ?? 0)
                      + " · " + i18n("Headquarters in %1", page.company.headquarters ?? "")
                font.bold: true
                elide: Text.ElideRight
            }

            QQC2.TabBar {
                currentIndex: ["garages", "drivers", "vehicles", "finances"].indexOf(page.view)

                QQC2.TabButton {
                    text: i18n("Garages")
                    width: implicitWidth
                    onClicked: page.view = "garages"
                }

                QQC2.TabButton {
                    text: i18n("Drivers")
                    width: implicitWidth
                    onClicked: page.view = "drivers"
                }

                QQC2.TabButton {
                    text: i18n("Vehicles")
                    width: implicitWidth
                    onClicked: page.view = "vehicles"
                }

                QQC2.TabButton {
                    text: i18n("Finances")
                    width: implicitWidth
                    onClicked: page.view = "finances"
                }
            }
        }
    }

    ListView {
        id: list

        model: !page.loaded ? []
             : page.view === "garages" ? page.company.garages
             : page.view === "drivers" ? page.company.drivers
             : page.view === "vehicles" ? page.company.vehicles
             : page.company.repairs
        spacing: 0
        delegate: page.view === "garages" ? garageDelegate
                : page.view === "drivers" ? driverDelegate
                : page.view === "vehicles" ? vehicleDelegate
                : repairDelegate

        section.property: page.view === "vehicles" ? "section" : ""
        section.delegate: Kirigami.ListSectionHeader {
            required property string section

            width: ListView.view.width
            text: section
        }

        header: page.view === "finances" ? financesHeader : null

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !page.loaded && !Wheelstar.careerLoading
            icon.name: "office-building"
            text: Wheelstar.careerError.length > 0 ? Wheelstar.careerError : i18n("No save game read yet")
        }

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: Wheelstar.careerLoading
            visible: running
        }
    }

    /// A row of the lists: a title and a line under it on the left, up to two figures on the right.
    component ListRow: QQC2.ItemDelegate {
        id: row

        property string title
        property string subtitle
        property string detail
        property string first
        property string second

        width: ListView.view.width
        hoverEnabled: false
        down: false

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    Layout.fillWidth: true
                    text: row.title
                    font.bold: true
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: row.subtitle
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: row.detail.length > 0
                    text: row.detail
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            QQC2.Label {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                horizontalAlignment: Text.AlignRight
                text: row.first
                font.features: { "tnum": 1 }
            }

            QQC2.Label {
                Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                horizontalAlignment: Text.AlignRight
                text: row.second
                font.features: { "tnum": 1 }
            }
        }
    }

    Component {
        id: garageDelegate

        ListRow {
            required property var modelData

            title: modelData.headquarters ? i18n("%1 · Headquarters", modelData.city) : modelData.city
            // i18n: %1 trucks in the garage, %2 the slots it has, %3 its trailers
            subtitle: i18np("%2 slots, %1 truck", "%2 slots, %1 trucks", modelData.trucks, modelData.slots) + " · "
                      + i18np("%1 trailer", "%1 trailers", modelData.trailers)
            // i18n: how busy a garage is, in percent
            first: i18n("%1 busy", page.percent(modelData.productivity))
            second: page.money(modelData.revenue)
        }
    }

    Component {
        id: driverDelegate

        ListRow {
            required property var modelData

            // i18n: %1 the driver, %2 the city of the garage
            title: i18n("%1 · %2", modelData.name, modelData.garage)
            subtitle: modelData.job.cargo
                      // i18n: the job a driver is on: %1 the cargo, %2 and %3 the cities
                      ? i18n("%1, %2 → %3", modelData.job.cargo, modelData.job.from, modelData.job.to)
                      : i18n("No job")
            // i18n: %1 the driver's hometown, %2 where the driver is now, %3 the truck
            detail: i18n("From %1 · now in %2 · %3", modelData.hometown, modelData.city, modelData.truck) + " · "
                    // i18n: a driver's skills, 0 to 6 each: ADR classes, long distance, high-value cargo, fragile
                    // cargo, just-in-time delivery, ecodriving
                    + i18n("ADR %1, long distance %2, high value %3, fragile %4, just in time %5, ecodriving %6",
                           modelData.skills[0], modelData.skills[1], modelData.skills[2], modelData.skills[3],
                           modelData.skills[4], modelData.skills[5])
            first: i18n("%1 XP", page.number(modelData.experience))
            second: page.money(modelData.revenue)
        }
    }

    Component {
        id: vehicleDelegate

        ListRow {
            required property var modelData

            title: modelData.plate ? i18n("%1 · %2", modelData.name, modelData.plate) : modelData.name
            subtitle: modelData.driver ? i18n("%1 · %2", modelData.garage, modelData.driver) : modelData.garage
            // i18n: %1 a distance in kilometres
            first: i18n("%1 km", page.number(modelData.kilometres))
            // i18n: how worn a vehicle is, its worst part, in percent
            second: i18n("%1 worn", page.percent(modelData.wear))
        }
    }

    Component {
        id: repairDelegate

        ListRow {
            required property var modelData

            title: i18n("Repair")
            // i18n: %1 the in-game day
            subtitle: i18n("Day %1", modelData.day)
            second: page.money(modelData.amount)
        }
    }

    Component {
        id: financesHeader

        ColumnLayout {
            width: list.width
            spacing: Kirigami.Units.largeSpacing

            Kirigami.FormLayout {
                Layout.fillWidth: true

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Bank")
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Money:")
                    text: page.money(page.company.bank.money)
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Loans:")
                    // i18n: %1 the loans that run, %2 how much the bank would lend
                    text: i18np("%1 loan, up to %2", "%1 loans, up to %2", page.company.bank.loans,
                                page.money(page.company.bank.loanLimit))
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Insurance:")
                    // i18n: what the player pays of an accident: %1 a fixed sum, %2 a share of the rest
                    text: i18n("%1 and %2 of each accident", page.money(page.company.bank.coinsuranceFixed),
                               page.percent(page.company.bank.coinsuranceRatio))
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Overdraft:")
                    text: page.company.bank.overdraft ? i18n("Yes") : i18n("No")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("The last days")
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Garages:")
                    // i18n: %1 what came in, %2 what went out (wages, upkeep, fuel)
                    text: i18n("%1 in, %2 out", page.money(page.company.revenue), page.money(page.company.costs))
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Your own driving:")
                    text: i18n("%1 in, %2 out", page.money(page.company.ownRevenue), page.money(page.company.ownCosts))
                }

                QQC2.Label {
                    Kirigami.FormData.label: i18n("Repairs:")
                    text: page.money(page.company.repairsTotal)
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.disabledTextColor
                text: i18n("The game keeps only the last few in-game days of these figures; the repairs below are those it still remembers.")
            }
        }
    }
}
