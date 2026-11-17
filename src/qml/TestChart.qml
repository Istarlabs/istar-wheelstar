// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.quickcharts as Charts
import org.kde.quickcharts.controls as ChartsControls

/// One result chart: lines on a left axis and, optionally, a right one, as Controller.chartData() returns them.
/// KQuickCharts has no second y axis, so it is a second chart drawn over the first.
ColumnLayout {
    id: root

    property string title
    property string xLabel
    property string leftLabel
    property string rightLabel
    property var chart: ({})

    readonly property var leftAxis: chart.left ?? { from: 0, to: 1, series: [] }
    readonly property var rightAxis: chart.right ?? { from: 0, to: 1, series: [] }
    readonly property bool hasRightAxis: rightAxis.series.length > 0
    readonly property color gridColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                               Kirigami.Theme.textColor, 0.15)

    function formatted(value: real, from: real, to: real): string {
        const span = Math.abs(to - from);
        return Number(value).toFixed(span < 2 ? 2 : span < 20 ? 1 : 0);
    }

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Heading {
        Layout.alignment: Qt.AlignHCenter
        level: 4
        text: root.title
    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.largeSpacing

        Repeater {
            model: root.leftAxis.series.concat(root.rightAxis.series)

            delegate: Row {
                required property var modelData

                spacing: Kirigami.Units.smallSpacing

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Kirigami.Units.gridUnit
                    height: 3
                    color: modelData.color
                }

                QQC2.Label {
                    text: modelData.name
                    font: Kirigami.Theme.smallFont
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        columns: 3
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: 0

        QQC2.Label {
            text: root.leftLabel
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
        }

        Item {
            Layout.fillWidth: true
        }

        QQC2.Label {
            text: root.hasRightAxis ? root.rightLabel : ""
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
        }

        ChartsControls.AxisLabels {
            Layout.fillHeight: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
            direction: ChartsControls.AxisLabels.VerticalBottomTop
            source: Charts.ChartAxisSource {
                chart: leftChart
                axis: Charts.ChartAxisSource.YAxis
                itemCount: 5
            }
            delegate: QQC2.Label {
                anchors.right: parent ? parent.right : undefined
                text: root.formatted(ChartsControls.AxisLabels.label, root.leftAxis.from, root.leftAxis.to)
                font: Kirigami.Theme.smallFont
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 6
            color: "transparent"
            border.color: root.gridColor

            ChartsControls.GridLines {
                anchors.fill: parent
                chart: leftChart
                direction: ChartsControls.GridLines.Vertical
                major.count: 7
                major.color: root.gridColor
                minor.visible: false
            }

            ChartsControls.GridLines {
                anchors.fill: parent
                chart: leftChart
                direction: ChartsControls.GridLines.Horizontal
                major.count: 3
                major.color: root.gridColor
                minor.visible: false
            }

            Charts.LineChart {
                id: leftChart

                anchors.fill: parent
                lineWidth: 1.5
                fillOpacity: 0
                yRange.automatic: false
                yRange.from: root.leftAxis.from
                yRange.to: root.leftAxis.to
                colorSource: Charts.ArraySource {
                    array: root.leftAxis.series.map(line => line.color)
                }
                nameSource: Charts.ArraySource {
                    array: root.leftAxis.series.map(line => line.name)
                }

                Instantiator {
                    model: root.leftAxis.series
                    delegate: Charts.ArraySource {
                        required property var modelData

                        array: modelData.values
                    }
                    onObjectAdded: (index, source) => leftChart.insertValueSource(index, source)
                    onObjectRemoved: (index, source) => leftChart.removeValueSource(source)
                }
            }

            Charts.LineChart {
                id: rightChart

                anchors.fill: parent
                visible: root.hasRightAxis
                lineWidth: 1.5
                fillOpacity: 0
                yRange.automatic: false
                yRange.from: root.rightAxis.from
                yRange.to: root.rightAxis.to
                colorSource: Charts.ArraySource {
                    array: root.rightAxis.series.map(line => line.color)
                }
                nameSource: Charts.ArraySource {
                    array: root.rightAxis.series.map(line => line.name)
                }

                Instantiator {
                    model: root.rightAxis.series
                    delegate: Charts.ArraySource {
                        required property var modelData

                        array: modelData.values
                    }
                    onObjectAdded: (index, source) => rightChart.insertValueSource(index, source)
                    onObjectRemoved: (index, source) => rightChart.removeValueSource(source)
                }
            }
        }

        ChartsControls.AxisLabels {
            Layout.fillHeight: true
            Layout.preferredWidth: root.hasRightAxis ? Kirigami.Units.gridUnit * 3 : 0
            visible: root.hasRightAxis
            direction: ChartsControls.AxisLabels.VerticalBottomTop
            source: Charts.ChartAxisSource {
                chart: rightChart
                axis: Charts.ChartAxisSource.YAxis
                itemCount: 5
            }
            delegate: QQC2.Label {
                text: root.formatted(ChartsControls.AxisLabels.label, root.rightAxis.from, root.rightAxis.to)
                font: Kirigami.Theme.smallFont
            }
        }

        Item {}

        // The x axis is seconds; the chart itself only knows sample indices.
        ChartsControls.AxisLabels {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit
            direction: ChartsControls.AxisLabels.HorizontalLeftRight
            source: Charts.ArraySource {
                array: [0, 1, 2, 3, 4, 5, 6, 7, 8].map(step => ((root.chart.duration ?? 0) * step / 8).toFixed(1))
            }
            delegate: QQC2.Label {
                text: ChartsControls.AxisLabels.label
                font: Kirigami.Theme.smallFont
            }
        }

        Item {}

        Item {}

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.xLabel.length > 0
            text: root.xLabel
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
        }

        Item {}
    }
}
