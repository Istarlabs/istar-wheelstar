// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

FormCard.FormCardPage {
    title: i18n("Tools")

    FormCard.FormHeader {
        title: i18n("Tools")
    }

    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18n("Center wheel")
            description: i18n("Center the wheel immediately after this option is enabled. Works also when setting it from a profile.")
            enabled: Wheelstar.hasDevice
            checked: Wheelstar.settings.center_wheel ?? false
            onToggled: Wheelstar.setSetting("center_wheel", checked)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSwitchDelegate {
            text: i18n("Start application manually")
            description: i18n("Start the companion application manually from the GUI.")
            enabled: Wheelstar.hasDevice
            checked: Wheelstar.settings.start_app_manually ?? false
            onToggled: Wheelstar.setSetting("start_app_manually", checked)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSwitchDelegate {
            text: i18n("Enable wheel buttons")
            description: i18n("Use wheel buttons to control some wheel settings. The buttons must be defined in the preferences window.")
            enabled: Wheelstar.settings.use_buttons !== undefined
            checked: Wheelstar.settings.use_buttons ?? false
            onToggled: Wheelstar.setSetting("use_buttons", checked)
        }
    }

    FormCard.FormHeader {
        title: i18n("Wheel range display")
    }

    FormCard.FormCard {
        FormCard.FormSectionText {
            text: i18n("Enable wheel range settings in the window overlay. When set to auto, it will be displayed only when being set using the wheel buttons.")
        }

        Repeater {
            model: [
                { value: "never", text: i18n("Never") },
                { value: "always", text: i18n("Always") },
                { value: "auto", text: i18n("Auto") }
            ]

            delegate: FormCard.FormRadioDelegate {
                required property var modelData

                text: modelData.text
                enabled: Wheelstar.settings.range_overlay !== undefined
                checked: (Wheelstar.settings.range_overlay ?? "never") === modelData.value
                onToggled: if (checked) Wheelstar.setSetting("range_overlay", modelData.value)
            }
        }
    }

    FormCard.FormHeader {
        title: i18n("FFBmeter")
    }

    FormCard.FormCard {
        FormCard.FormSectionText {
            text: i18n("Enable FFBmeter using the leds in the wheel or the window overlay.")
        }

        FormCard.FormCheckDelegate {
            text: i18n("Leds")
            enabled: Wheelstar.settings.ffb_leds !== undefined
            checked: Wheelstar.settings.ffb_leds ?? false
            onToggled: Wheelstar.setSetting("ffb_leds", checked)
        }

        FormCard.FormCheckDelegate {
            text: i18n("Overlay")
            enabled: Wheelstar.settings.ffb_overlay !== undefined
            checked: Wheelstar.settings.ffb_overlay ?? false
            onToggled: Wheelstar.setSetting("ffb_overlay", checked)
        }
    }
}
