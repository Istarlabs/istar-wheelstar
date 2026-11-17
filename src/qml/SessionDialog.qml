// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.quickcharts as Charts
import org.kde.quickcharts.controls as ChartsControls

/// One drive in detail: the figures, how the wheel was held, and which gear the truck was in.
Kirigami.Dialog {
    id: dialog

    /// A session as the controller hands it out.
    property var session: ({})
    /// The protocol 3 figures of the drive, null for an older one.
    readonly property var details: session.details ?? null
    /// The page's formatting, so a duration reads the same in the list and here.
    property var formatDuration
    property var formatSpan
    property var formatKilometres

    /// A bar chart of shares in percent, with an axis in percent and, when there are labels, one under every bar.
    component ShareChart: GridLayout {
        id: share

        property var values: []
        property var labels: []

        readonly property color gridColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                                   Kirigami.Theme.textColor, 0.15)
        // The axis ends on a multiple of 5, so its labels are round numbers: the highest bar divided by 5, rounded
        // up, times 5 again (23 % gives 25). At least 5, so an empty chart still has an axis.
        readonly property real axisMaximum: Math.max(5, Math.ceil(Math.max(0, ...values) / 5) * 5)

        columns: 2
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: 0

        ChartsControls.AxisLabels {
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
            direction: ChartsControls.AxisLabels.VerticalBottomTop
            source: Charts.ChartAxisSource {
                chart: bars
                axis: Charts.ChartAxisSource.YAxis
                itemCount: 3
            }
            delegate: QQC2.Label {
                anchors.right: parent ? parent.right : undefined
                text: Number(ChartsControls.AxisLabels.label).toFixed(0) + " %"
                font: Kirigami.Theme.smallFont
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 6
            color: "transparent"
            border.color: share.gridColor

            ChartsControls.GridLines {
                anchors.fill: parent
                chart: bars
                direction: ChartsControls.GridLines.Horizontal
                major.count: 3
                major.color: share.gridColor
                minor.visible: false
            }

            Charts.BarChart {
                id: bars

                anchors.fill: parent
                anchors.margins: 1
                spacing: 2
                yRange.automatic: false
                yRange.from: 0
                yRange.to: share.axisMaximum
                valueSources: Charts.ArraySource {
                    array: share.values
                }
                colorSource: Charts.SingleValueSource {
                    value: Kirigami.Theme.linkColor
                }
            }
        }

        Item {
            visible: share.labels.length > 0
        }

        ChartsControls.AxisLabels {
            Layout.fillWidth: true
            Layout.preferredHeight: share.labels.length > 0 ? Kirigami.Units.gridUnit : 0
            visible: share.labels.length > 0
            direction: ChartsControls.AxisLabels.HorizontalLeftRight
            source: Charts.ArraySource {
                array: share.labels
            }
            delegate: QQC2.Label {
                text: ChartsControls.AxisLabels.label
                font: Kirigami.Theme.smallFont
            }
        }
    }

    function show(data: var): void {
        session = data;
        open();
    }

    title: (session.game ?? "") + " · " + (session.startedAt ? formatSpan(session.startedAt, session.endedAt) : "")
    standardButtons: Kirigami.Dialog.Close
    preferredWidth: Kirigami.Units.gridUnit * 34
    padding: Kirigami.Units.largeSpacing

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.Label {
                Kirigami.FormData.label: i18n("Driving time:")
                text: dialog.session.drivingSeconds ? dialog.formatDuration(dialog.session.drivingSeconds) : ""
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Moving:")
                text: dialog.session.movingSeconds ? dialog.formatDuration(dialog.session.movingSeconds) : ""
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Distance:")
                text: dialog.formatKilometres ? dialog.formatKilometres(dialog.session.distanceKilometres, 1) : ""
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Average speed:")
                text: i18n("%1 km/h", Math.round(dialog.session.averageKmh ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Top speed:")
                text: i18n("%1 km/h", Math.round(dialog.session.maxKmh ?? 0))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Steering corrections:")
                text: i18n("%1 (%2 per minute)", dialog.session.corrections ?? 0, (dialog.session.correctionsPerMinute ?? 0).toFixed(1))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Hard brakes:")
                text: dialog.session.hardBrakes ?? 0
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Throttle released:")
                text: i18n("%1 % of the time moving", Math.round((dialog.session.throttleIdle ?? 0) * 100))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Throttle floored:")
                text: i18n("%1 % of the time moving", Math.round((dialog.session.throttleFull ?? 0) * 100))
            }

            QQC2.Label {
                Kirigami.FormData.label: i18n("Brake pressed:")
                text: i18n("%1 % of the time moving", Math.round((dialog.session.brake ?? 0) * 100))
            }

            // The rest came with plugin protocol 3; an older drive has none of it.
            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                visible: dialog.details !== null
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Over the speed limit:")
                // i18n: %1 the share of the distance driven more than 5 km/h over the limit, %2 the largest excess
                text: dialog.details ? i18n("%1 % of the distance, at most %2 km/h over", Math.round(dialog.details.speedingDistance * 100),
                                            Math.round(dialog.details.maxOverLimitKmh)) : ""
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Cruise control:")
                text: dialog.details ? i18n("%1 % of the time moving", Math.round(dialog.details.cruiseControl * 100)) : ""
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Retarder:")
                text: dialog.details ? i18n("%1 % of the time moving", Math.round(dialog.details.retarder * 100)) : ""
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Engine brake:")
                text: dialog.details ? i18n("%1 % of the time moving", Math.round(dialog.details.motorBrake * 100)) : ""
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Fuel:")
                text: !dialog.details ? ""
                      : dialog.details.fuelPer100Km === null || dialog.details.fuelPer100Km === undefined
                        ? i18n("%1 l", dialog.details.fuelLitres.toLocaleString(Qt.locale(), "f", 1))
                          // i18n: %1 litres burnt, %2 litres per 100 km of the game's distance
                        : i18n("%1 l (%2 l/100 km)", dialog.details.fuelLitres.toLocaleString(Qt.locale(), "f", 1),
                               dialog.details.fuelPer100Km.toLocaleString(Qt.locale(), "f", 1))
            }

            QQC2.Label {
                visible: dialog.details !== null
                Kirigami.FormData.label: i18n("Damage:")
                // i18n: how much the truck's wear and the cargo's damage rose during the drive, in percent
                text: dialog.details ? i18n("truck +%1 %, cargo +%2 %", (dialog.details.truckWear * 100).toLocaleString(Qt.locale(), "f", 1),
                                            (dialog.details.cargoDamage * 100).toLocaleString(Qt.locale(), "f", 1)) : ""
            }
        }

        Kirigami.Heading {
            level: 4
            visible: (dialog.session.events ?? []).length > 0
            text: i18n("Fines, tolls and crossings")
        }

        Repeater {
            model: dialog.session.events ?? []

            delegate: RowLayout {
                id: cost

                required property var modelData

                Layout.fillWidth: true

                QQC2.Label {
                    text: Qt.formatTime(cost.modelData.at, Qt.locale().timeFormat(Locale.ShortFormat))
                    color: Kirigami.Theme.disabledTextColor
                    font.features: { "tnum": 1 }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: cost.modelData.what
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    text: Number(cost.modelData.amount).toLocaleString(Qt.locale(), "f", 0) + " " + cost.modelData.currency
                    font.features: { "tnum": 1 }
                }
            }
        }

        Kirigami.Heading {
            level: 4
            text: i18n("Steering position")
        }

        ShareChart {
            Layout.fillWidth: true
            values: (dialog.session.steering ?? []).map(fraction => fraction * 100)
        }

        RowLayout {
            Layout.fillWidth: true

            QQC2.Label {
                text: i18n("Left")
                font: Kirigami.Theme.smallFont
            }

            Item {
                Layout.fillWidth: true
            }

            QQC2.Label {
                text: i18n("Right")
                font: Kirigami.Theme.smallFont
            }
        }

        Kirigami.Heading {
            level: 4
            text: i18n("Time in each gear")
        }

        ShareChart {
            Layout.fillWidth: true
            values: (dialog.session.gears ?? []).map(gear => gear.fraction * 100)
            labels: (dialog.session.gears ?? []).map(gear => gear.label)
        }
    }
}
