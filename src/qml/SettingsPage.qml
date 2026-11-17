// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

FormCard.FormCardPage {
    title: i18n("Wheelstar preferences")

    // Upstream stopped the button setup when the preferences window closed.
    Component.onDestruction: Wheelstar.stopButtonSetup()

    FormCard.FormHeader {
        title: i18n("Preferences")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Check permissions")
            description: i18n("Check permissions to read/write device settings and propose changes to make them available to normal users.")
            checked: Wheelstar.checkPermissions
            onToggled: Wheelstar.checkPermissions = checked
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormComboBoxDelegate {
            text: i18n("Language")
            description: i18n("Change the interface language. You'll need to restart the application to apply.")
            model: Wheelstar.languages
            textRole: "name"
            valueRole: "code"
            currentIndex: Wheelstar.languages.findIndex(language => language.code === Wheelstar.language)
            onActivated: Wheelstar.language = currentValue
        }
    }

    FormCard.FormHeader {
        title: i18n("Define buttons")
    }

    FormCard.FormCard {
        FormCard.FormSectionText {
            text: i18n("Pressing start will begin configuration of wheel buttons. Just press the button in the wheel that you want to use for every displayed action.")
        }

        FormCard.FormButtonDelegate {
            text: Wheelstar.buttonSetupText.length > 0 ? Wheelstar.buttonSetupText : i18n("Start")
            icon.name: Wheelstar.buttonSetupText.length > 0 ? "media-playback-stop" : "input-gaming"
            enabled: Wheelstar.hasDevice
            onClicked: Wheelstar.startStopButtonSetup()
        }
    }
}
