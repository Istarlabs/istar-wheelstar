// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/// A steering wheel that turns with the real one: a thick rim, a hub, two side spokes and a red mark at the
/// top. Drawn after a truck wheel; the red mark is what makes the rotation readable.
///
/// The ring around it lights up once the wheel is turned a full turn (360 degrees) from the centre.
Item {
    id: wheel

    /// Degrees from the centre, positive to the right (clockwise).
    property real angle: 0

    // 360 degrees are one turn: 540 degrees is one and a half.
    readonly property real turns: Math.abs(angle) / 360
    readonly property bool fullTurn: Math.abs(angle) >= 360

    // A black wheel whatever the theme; a faint disc behind it and a thin outline keep it visible on a dark one.
    readonly property color rimColor: "#141416"
    readonly property color hubColor: "#232327"
    readonly property color outlineColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                                  Kirigami.Theme.textColor.b, 0.35)
    readonly property color idleRingColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                                   Kirigami.Theme.textColor, 0.15)

    // Proportions chosen by eye, as a share of the width so the drawing scales: the ring is 2.5 % (at least 2
    // pixels, so it stays visible small) and the rim 10 %.
    readonly property real ringWidth: Math.max(2, width * 0.025)
    readonly property real rimWidth: width * 0.1

    implicitWidth: Kirigami.Units.gridUnit * 9
    implicitHeight: implicitWidth

    // The turn ring: it stays put and changes colour.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: wheel.ringWidth
        border.color: wheel.fullTurn ? Kirigami.Theme.neutralTextColor : wheel.idleRingColor
    }

    // The faint disc behind the wheel.
    Rectangle {
        anchors.fill: parent
        anchors.margins: wheel.ringWidth
        radius: width / 2
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.12)
    }

    Item {
        id: rotor

        anchors.fill: parent
        // A gap of one and a half ring widths between the ring and the wheel (the ring itself is one).
        anchors.margins: wheel.ringWidth * 2.5
        rotation: wheel.angle

        // The two side spokes, and the lower one, behind the hub.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - wheel.rimWidth
            height: wheel.rimWidth * 0.8
            radius: height / 2
            color: wheel.rimColor
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height / 2
            width: wheel.rimWidth * 0.8
            height: parent.height / 2 - wheel.rimWidth / 2
            radius: width / 2
            color: wheel.rimColor
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: wheel.rimWidth
            border.color: wheel.rimColor
        }

        // The outline of the rim.
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: wheel.outlineColor
        }

        // The stitching along the inside of the rim.
        Rectangle {
            anchors.fill: parent
            anchors.margins: wheel.rimWidth
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Kirigami.Theme.negativeTextColor
            opacity: 0.7
        }

        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.3
            height: width * 0.8
            radius: width * 0.18
            color: wheel.hubColor
        }

        // The mark at the top of the rim.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: wheel.rimWidth * 0.3
            height: wheel.rimWidth
            color: Kirigami.Theme.negativeTextColor
        }
    }
}
