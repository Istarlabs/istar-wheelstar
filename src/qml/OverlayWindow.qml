// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The small always-on-top window with the rotation range and the FFB meter, for use while a game runs.
/// It can be dragged anywhere. On Wayland, staying on top is up to the compositor (a KWin window rule).
Window {
    id: overlay

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    title: i18n("Wheelstar")
    visible: Wheelstar.overlayVisible
    width: content.implicitWidth + Kirigami.Units.largeSpacing * 2
    height: content.implicitHeight + Kirigami.Units.largeSpacing * 2

    Rectangle {
        anchors.fill: parent
        radius: Kirigami.Units.cornerRadius * 2
        color: Qt.rgba(0, 0, 0, 0.7)

        DragHandler {
            target: null
            onActiveChanged: if (active) overlay.startSystemMove()
        }

        ColumnLayout {
            id: content

            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                visible: Wheelstar.overlayShowsRange

                QQC2.ToolButton {
                    icon.name: "list-remove"
                    icon.color: "white"
                    onClicked: Wheelstar.changeRangeBy(-10)
                }

                QQC2.Label {
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 4
                    horizontalAlignment: Text.AlignHCenter
                    text: (Wheelstar.settings.range ?? 0) + "°"
                    color: "white"
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 2
                    font.bold: true
                    font.features: { "tnum": 1 }
                }

                QQC2.ToolButton {
                    icon.name: "list-add"
                    icon.color: "white"
                    onClicked: Wheelstar.changeRangeBy(10)
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                visible: Wheelstar.overlayShowsFfbMeter
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    // The rim LEDs, green to red.
                    model: ["#2ecc71", "#2ecc71", "#f1c40f", "#e67e22", "#e74c3c"]

                    delegate: Rectangle {
                        required property string modelData
                        required property int index

                        width: Kirigami.Units.gridUnit
                        height: width
                        radius: width / 2
                        color: modelData
                        opacity: (Wheelstar.ffbMeterLeds & (1 << index)) !== 0 ? 1 : 0.15
                    }
                }
            }
        }
    }
}
