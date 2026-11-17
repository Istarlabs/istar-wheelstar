// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The live state of the wheel: steering, pedals, hat and buttons.
///
/// Drawn with the theme's semantic colours rather than its highlight colour: an accent can be a dark grey,
/// and a pedal bar in it is invisible on a dark background.
GridLayout {
    id: panel

    /// The colour anything "active" is drawn in: a pressed button, the hat direction.
    readonly property color activeColor: Kirigami.Theme.linkColor
    readonly property color trackColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                                Kirigami.Theme.textColor, 0.08)
    readonly property color borderColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                                 Kirigami.Theme.textColor, 0.25)

    // By button number as text: what the user calls the button on this wheel, and what it does in this profile.
    readonly property var buttonNames: Wheelstar.buttonNames
    readonly property var buttonNotes: Wheelstar.settings.button_notes ?? ({})

    /// A pedal: a bar filled from the bottom, `value` 0 to 1, with the percentage on it and the name below.
    component PedalBar: ColumnLayout {
        id: pedal

        property real value
        property color fillColor
        property string label

        Layout.fillHeight: true
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 4
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5
            radius: Kirigami.Units.cornerRadius
            color: panel.trackColor
            border.color: panel.borderColor

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Math.max(0, Math.min(1, pedal.value)) * parent.height
                radius: parent.radius
                color: pedal.fillColor
            }

            QQC2.Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: Kirigami.Units.smallSpacing
                text: Math.round(pedal.value * 100) + " %"
                font: Kirigami.Theme.smallFont
            }
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            text: pedal.label
        }
    }

    columns: 2
    columnSpacing: Kirigami.Units.largeSpacing
    rowSpacing: Kirigami.Units.largeSpacing * 2

    Layout.margins: Kirigami.Units.largeSpacing

    RowLayout {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        spacing: Kirigami.Units.gridUnit * 2

        Item {
            Layout.fillWidth: true
        }

        SteeringWheel {
            id: wheel

            // The steering runs from -1 to 1 over the whole range, so one side is half of it: at 900 degrees full
            // lock is 450 degrees either way. Without a range set, the wheel's own maximum is what it uses.
            angle: Wheelstar.steering * (Wheelstar.settings.range ?? Wheelstar.maxRange) / 2
        }

        ColumnLayout {
            id: readout

            // As wide as the widest angle ("-000°"), so a third digit does not push the pedals aside.
            readonly property real fixedWidth: Math.max(angleMetrics.advanceWidth, turnsLabel.implicitWidth)

            Layout.alignment: Qt.AlignVCenter
            Layout.minimumWidth: fixedWidth
            Layout.preferredWidth: fixedWidth
            Layout.maximumWidth: fixedWidth
            spacing: Kirigami.Units.smallSpacing

            TextMetrics {
                id: angleMetrics

                font: angleLabel.font
                text: "-000°"
            }

            QQC2.Label {
                id: angleLabel

                text: Math.round(wheel.angle) + "°"
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 2
                font.bold: true
                font.features: { "tnum": 1 }
            }

            QQC2.Label {
                id: turnsLabel

                // i18n: %1 is how far the wheel is turned from the centre, in turns with one decimal, like 1.2
                text: i18n("%1 turns", wheel.turns.toLocaleString(Qt.locale(), "f", 1))
                color: wheel.fullTurn ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.disabledTextColor
                font.features: { "tnum": 1 }
            }
        }

        RowLayout {
            Layout.fillHeight: true
            spacing: Kirigami.Units.largeSpacing

            PedalBar {
                label: i18n("Clutch")
                value: Wheelstar.clutch
                fillColor: Kirigami.Theme.neutralTextColor
            }

            PedalBar {
                label: i18n("Brakes")
                value: Wheelstar.brake
                fillColor: Kirigami.Theme.negativeTextColor
            }

            PedalBar {
                label: i18n("Accel.")
                value: Wheelstar.throttle
                fillColor: Kirigami.Theme.positiveTextColor
            }
        }

        Item {
            Layout.fillWidth: true
        }
    }

    // What the telemetry plugin in the game reports; there while the game is simulating.
    RowLayout {
        Layout.columnSpan: 2
        Layout.alignment: Qt.AlignHCenter
        visible: Wheelstar.telemetryActive
        spacing: Kirigami.Units.gridUnit

        QQC2.Label {
            text: Wheelstar.telemetryGame
            font.bold: true
        }

        QQC2.Label {
            text: i18n("%1 km/h", Math.round(Wheelstar.telemetrySpeed))
            font.features: { "tnum": 1 }
        }

        QQC2.Label {
            readonly property int gear: Wheelstar.telemetryGear

            // i18n: %1 is the gear the truck is in: a number, R for reverse or N for neutral
            text: i18n("Gear %1", gear > 0 ? gear : (gear < 0 ? "R" : "N"))
        }
    }

    Kirigami.Separator {
        Layout.columnSpan: 2
        Layout.fillWidth: true
    }

    QQC2.Label {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 5
        text: i18n("Hat")
    }

    Grid {
        columns: 3
        spacing: 2

        Repeater {
            // Up, left, right and down around an empty middle, each as [axis, value]. Each direction looks at its
            // own axis only, so a diagonal lights both of its directions.
            model: [null, ["y", -1], null, ["x", -1], null, ["x", 1], null, ["y", 1], null]

            delegate: Rectangle {
                required property var modelData

                readonly property bool active: modelData !== null
                                               && (modelData[0] === "x" ? Wheelstar.hatX : Wheelstar.hatY) === modelData[1]

                width: Kirigami.Units.gridUnit
                height: width
                radius: 2
                opacity: modelData === null ? 0 : 1
                color: active ? panel.activeColor : panel.trackColor
                border.color: panel.borderColor
            }
        }
    }

    QQC2.Label {
        Layout.alignment: Qt.AlignTop
        text: i18n("Buttons")
    }

    Kirigami.PromptDialog {
        id: buttonDialog

        property int index

        function openFor(button) {
            index = button;
            open();
        }

        title: i18n("Button %1", index)
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Cancel
        onOpened: {
            nameField.text = panel.buttonNames[index] ?? "";
            noteField.text = panel.buttonNotes[index] ?? "";
            nameField.forceActiveFocus();
        }
        onAccepted: {
            Wheelstar.setButtonName(index, nameField.text);

            if (noteField.visible) {
                Wheelstar.setButtonNote(index, noteField.text);
            }
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: nameField

                Layout.fillWidth: true
                placeholderText: i18n("Name on the wheel, like X or Left paddle")
                onAccepted: buttonDialog.accept()
            }

            // The symbols of a PlayStation controller, which the keyboard does not have.
            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: ["□", "△", "○", "✕"]

                    delegate: QQC2.Button {
                        required property string modelData

                        text: modelData
                        onClicked: {
                            nameField.insert(nameField.cursorPosition, modelData);
                            nameField.forceActiveFocus();
                        }
                    }
                }
            }

            QQC2.TextField {
                id: noteField

                Layout.fillWidth: true
                // A note belongs to a profile, which is what is tied to a game.
                visible: Wheelstar.currentProfile !== ""
                placeholderText: i18n("What it does in this profile, like Horn")
                onAccepted: buttonDialog.accept()
            }
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: 28

            delegate: Rectangle {
                required property int index

                readonly property bool pressed: (Wheelstar.buttons & (1 << index)) !== 0

                // The number the game knows it by, then the name the user gave it; it grows to fit.
                readonly property string caption: panel.buttonNames[index] ? index + " · " + panel.buttonNames[index] : index
                readonly property bool named: !!panel.buttonNames[index] || !!panel.buttonNotes[index]
                // A button that was pressed stays, so there is something to click and name.
                property bool seen: false

                visible: Wheelstar.showUnusedButtons || named || seen
                onPressedChanged: {
                    if (pressed) {
                        seen = true;
                    }
                }

                width: Math.max(Kirigami.Units.gridUnit * 1.6, captionLabel.implicitWidth + Kirigami.Units.largeSpacing * 2)
                height: Kirigami.Units.gridUnit * 1.6
                radius: height / 2
                color: pressed ? panel.activeColor : panel.trackColor
                border.color: pressed ? panel.activeColor : panel.borderColor

                HoverHandler {
                    id: hover

                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    onTapped: buttonDialog.openFor(parent.index)
                }

                QQC2.ToolTip {
                    visible: hover.hovered
                    delay: Kirigami.Units.toolTipDelay
                    text: {
                        const name = panel.buttonNames[parent.index];
                        const note = panel.buttonNotes[parent.index];
                        return i18n("Button %1", parent.index) + (name ? " — " + name : "") + (note ? "\n" + note : "");
                    }
                }

                QQC2.Label {
                    id: captionLabel

                    anchors.centerIn: parent
                    text: parent.caption
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    font.bold: parent.pressed
                    color: parent.pressed ? Kirigami.Theme.backgroundColor : Kirigami.Theme.textColor
                }
            }
        }
    }

    QQC2.CheckBox {
        Layout.columnSpan: 2
        text: i18n("Show unused buttons")
        checked: Wheelstar.showUnusedButtons
        onToggled: Wheelstar.showUnusedButtons = checked

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        QQC2.ToolTip.text: i18n("A button without a name shows up as soon as you press it.")
    }
}
