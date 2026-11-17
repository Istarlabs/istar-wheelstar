// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// Upstream's matplotlib figure: the linearity chart, the results, and the two step test charts.
QQC2.ApplicationWindow {
    id: window

    function refill(): void {
        linearChart.chart = Wheelstar.chartData(0);
        positionChart.chart = Wheelstar.chartData(1);
        accelerationChart.chart = Wheelstar.chartData(2);
    }

    title: i18n("Tests")
    width: Kirigami.Units.gridUnit * 64
    height: Kirigami.Units.gridUnit * 42
    color: Kirigami.Theme.backgroundColor

    Component.onCompleted: refill()

    // chartData() is a call, not a property, so the charts are refilled whenever the report changes.
    Connections {
        target: Wheelstar

        function onTestChanged(): void {
            window.refill();
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        columns: 2
        columnSpacing: Kirigami.Units.largeSpacing * 2
        rowSpacing: Kirigami.Units.largeSpacing * 2

        TestChart {
            id: linearChart

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            title: i18n("Linear response test")
            leftLabel: i18n("Force level")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1

            Repeater {
                model: [
                    { key: "latency", text: i18n("Latency (ms):") },
                    { key: "maxVelocity", text: i18n("Max. angular velocity (RPM):") },
                    { key: "meanAcceleration", text: i18n("Mean angular acceleration (RPM/s):") },
                    { key: "meanDeceleration", text: i18n("Mean angular deceleration (RPM/s):") },
                    { key: "maxAcceleration", text: i18n("Max. angular acceleration (RPM/s):") },
                    { key: "timeToMaxAcceleration", text: i18n("Time to max. angular acceleration (ms):") },
                    { key: "maxDeceleration", text: i18n("Max. angular deceleration (RPM/s):") },
                    { key: "timeToMaxDeceleration", text: i18n("Time to max. angular deceleration (ms):") },
                    { key: "residualDeceleration", text: i18n("Residual angular deceleration (RPM/s):") },
                    { key: "estimatedSnr", text: i18n("Estimated SNR (dB):") },
                    { key: "minimumLevel", text: i18n("Min. force level (%):") }
                ]

                delegate: QQC2.Label {
                    required property var modelData

                    Kirigami.FormData.label: modelData.text
                    text: Wheelstar.testResults[modelData.key] ?? ""
                }
            }
        }

        TestChart {
            id: positionChart

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            title: i18n("Step test (angular velocity + position)")
            xLabel: i18n("Time (s)")
            leftLabel: i18n("Position")
            rightLabel: i18n("RPM")
        }

        TestChart {
            id: accelerationChart

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            title: i18n("Step test (angular acceleration)")
            xLabel: i18n("Time (s)")
            rightLabel: i18n("RPM/s")
        }
    }
}
