// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

FormCard.FormCardPage {
    title: i18n("Force Feedback")

    FormCard.FormHeader {
        title: i18n("Force Feedback")
    }

    FormCard.FormCard {
        Repeater {
            model: [
                { key: "ff_gain", text: i18n("Global feedback gain"),
                  description: i18n("Global strength for all force feedback effects.") },
                { key: "spring_level", text: i18n("Spring effect gain"),
                  description: i18n("Gain applied to spring effects. Higher values may produce more clipping.") },
                { key: "damper_level", text: i18n("Damper effect gain"),
                  description: i18n("Gain applied to damper effects. Higher values may produce more clipping.") },
                { key: "friction_level", text: i18n("Friction effect gain"),
                  description: i18n("Gain applied to friction effects. Higher values may produce more clipping.") },
                { key: "autocenter", text: i18n("Autocenter strength"),
                  description: i18n("Continuous autocenter force strength.") }
            ]

            delegate: SliderDelegate {
                required property var modelData

                text: modelData.text
                description: modelData.description
                value: Wheelstar.settings[modelData.key]
                from: 0
                to: 100
                stepSize: 1
                unit: " %"
                onMoved: value => Wheelstar.setSetting(modelData.key, value)
            }
        }
    }
}
