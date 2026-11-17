// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

/// A labelled slider row for a FormCard, showing its value; disabled when the setting is not supported.
FormCard.AbstractFormDelegate {
    id: delegate

    property string description
    property var value
    property alias from: slider.from
    property alias to: slider.to
    property alias stepSize: slider.stepSize
    property string unit

    signal moved(int value)

    enabled: value !== undefined
    background: null

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            QQC2.Label {
                Layout.fillWidth: true
                text: delegate.text
                elide: Text.ElideRight
            }

            QQC2.Label {
                text: delegate.value !== undefined ? Math.round(slider.value) + delegate.unit : "–"
                font.features: { "tnum": 1 }
            }
        }

        QQC2.Slider {
            id: slider

            Layout.fillWidth: true
            value: delegate.value !== undefined ? delegate.value : from
            snapMode: QQC2.Slider.SnapAlways
            onMoved: delegate.moved(Math.round(value))
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: delegate.description.length > 0
            text: delegate.description
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
        }
    }
}
