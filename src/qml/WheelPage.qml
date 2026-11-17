// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

FormCard.FormCardPage {
    id: page

    // The cards default to 30 grid units; wider lets the 28 buttons sit in fewer rows.
    readonly property real cardWidth: Kirigami.Units.gridUnit * 45

    title: i18n("Controls")

    actions: Kirigami.Action {
        text: i18n("Refresh")
        tooltip: i18n("Re-scan wheel devices and update window.")
        icon.name: "view-refresh"
        onTriggered: Wheelstar.refresh()
    }

    FormCard.FormHeader {
        title: i18n("Device")
        maximumWidth: page.cardWidth
    }

    FormCard.FormCard {
        maximumWidth: page.cardWidth

        FormCard.FormComboBoxDelegate {
            text: i18n("Device")
            description: i18n("Select the wheel controller you want to configure.")
            model: Wheelstar.devices
            textRole: "name"
            valueRole: "id"
            enabled: Wheelstar.devices.length > 0
            currentIndex: Wheelstar.devices.findIndex(device => device.id === Wheelstar.currentDeviceId)
            onActivated: Wheelstar.currentDeviceId = currentValue
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            id: profileBox

            text: i18n("Profile")
            description: i18n("Select a profile to load the saved settings.")
            enabled: Wheelstar.hasDevice
            model: [""].concat(Wheelstar.profiles)
            displayText: currentIndex > 0 ? currentText : i18n("None")
            currentIndex: Math.max(0, model.indexOf(Wheelstar.currentProfile))
            onActivated: Wheelstar.loadProfile(currentValue)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18n("Save profile")
            icon.name: "document-save"
            enabled: Wheelstar.profileDirty
            onClicked: Wheelstar.saveProfile()
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18n("New profile…")
            icon.name: "document-new"
            enabled: Wheelstar.hasDevice
            onClicked: newProfileDialog.open()
        }
    }

    FormCard.FormHeader {
        title: i18n("Wheel")
        maximumWidth: page.cardWidth
    }

    FormCard.FormCard {
        maximumWidth: page.cardWidth

        FormCard.FormComboBoxDelegate {
            id: modeBox

            text: i18n("Compatibility mode")
            description: i18n("Select an emulation mode from the ones supported.")
            enabled: Wheelstar.modes.length > 0
            model: Wheelstar.modes
            textRole: "name"
            valueRole: "id"
            currentIndex: Wheelstar.modes.findIndex(mode => mode.id === Wheelstar.settings.mode)
        }

        FormCard.FormButtonDelegate {
            text: i18n("Apply")
            description: i18n("Apply changes (the wheel may move when changing the emulation mode).")
            icon.name: "dialog-ok-apply"
            visible: Wheelstar.modes.length > 1
            enabled: modeBox.currentValue !== undefined && modeBox.currentValue !== Wheelstar.settings.mode
            onClicked: Wheelstar.applyMode(modeBox.currentValue)
        }

        FormCard.FormDelegateSeparator {}

        SliderDelegate {
            text: i18n("Rotation range")
            description: i18n("Range in degrees that the wheel will be able to rotate both ways. It will affect the sensitivity.")
            value: Wheelstar.settings.range
            from: 40
            to: Wheelstar.maxRange
            stepSize: 10
            unit: "°"
            onMoved: value => Wheelstar.setSetting("range", value)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSectionText {
            Layout.maximumWidth: page.cardWidth
            text: i18n("Combine pedals") + " — " + i18n("Combine two pedals into one axe. Useful for old games and flight simulators.")
        }

        Repeater {
            model: [i18n("None"), i18n("Accelerator + Brakes"), i18n("Accelerator + Clutch")]

            delegate: FormCard.FormRadioDelegate {
                required property string modelData
                required property int index

                text: modelData
                enabled: Wheelstar.settings.combine_pedals !== undefined
                checked: (Wheelstar.settings.combine_pedals ?? 0) === index
                onToggled: if (checked) Wheelstar.setSetting("combine_pedals", index)
            }
        }
    }

    FormCard.FormHeader {
        title: i18n("Inputs")
        maximumWidth: page.cardWidth
    }

    FormCard.FormCard {
        maximumWidth: page.cardWidth

        InputPanel {
            Layout.fillWidth: true
        }
    }

    Kirigami.PromptDialog {
        id: newProfileDialog

        title: i18n("New profile")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Cancel
        onOpened: {
            nameField.text = "";
            nameField.forceActiveFocus();
        }
        onAccepted: {
            if (Wheelstar.profileExists(nameField.text.trim())) {
                overwriteDialog.name = nameField.text.trim();
                overwriteDialog.open();
            } else {
                Wheelstar.createProfile(nameField.text);
            }
        }

        QQC2.TextField {
            id: nameField

            placeholderText: i18n("Profile name")
            onAccepted: newProfileDialog.accept()
        }
    }

    Kirigami.PromptDialog {
        id: overwriteDialog

        property string name

        title: i18n("This profile already exists. Are you sure?")
        subtitle: name
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        onAccepted: Wheelstar.createProfile(name)
    }
}
