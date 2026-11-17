// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

FormCard.FormCardPage {
    id: page

    signal openChart()

    readonly property int stage: Wheelstar.testStage
    readonly property bool prompt: stage === Wheelstar.MinimumForcePrompt || stage === Wheelstar.LinearityPrompt
                                   || stage === Wheelstar.StepPrompt
    readonly property bool busy: stage === Wheelstar.MinimumForceReady || stage === Wheelstar.MinimumForceRunning
                                 || stage === Wheelstar.Running

    title: i18n("Tests")

    actions: [
        Kirigami.Action {
            text: i18n("Start new")
            icon.name: "media-playback-start"
            enabled: Wheelstar.hasDevice && !page.busy
            onTriggered: Wheelstar.startTest()
        },
        Kirigami.Action {
            text: i18n("Import CSV")
            icon.name: "document-import"
            enabled: !page.busy
            onTriggered: importDialog.open()
        },
        Kirigami.Action {
            text: i18n("Export CSV")
            icon.name: "document-export"
            enabled: Wheelstar.hasTestReport && !page.busy
            onTriggered: {
                exportDialog.selectedFile = Wheelstar.defaultReportName();
                exportDialog.open();
            }
        },
        Kirigami.Action {
            text: i18n("Open chart")
            icon.name: "office-chart-line"
            enabled: Wheelstar.hasTestReport && !page.busy
            onTriggered: page.openChart()
        }
    ]

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.Idle
            text: i18n("No test data available. Please, import test data or start a new test.")
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.MinimumForcePrompt
            text: i18n("The lowest torque test measures the minimum force level that the wheel can deliver. Your help is required for this test.")
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.MinimumForceReady || page.stage === Wheelstar.MinimumForceRunning
            text: i18n("For the duration of this test you have to hold your steering wheel firmly in your hands and move it slowly around its middle position. When you notice a slight resistance or pulsation press any button in the wheel.")
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.MinimumForceReady
            text: i18n("Press any wheel button when ready to start the test.")
            font.bold: true
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.MinimumForceRunning
            text: i18n("Now running the test.")
            font.bold: true
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.LinearityPrompt
            text: i18n("The linearity test measures the linear force response.")
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.StepPrompt
            text: i18n("The step test measures several performance parameters at the highest force levels.")
        }

        FormCard.FormSectionText {
            visible: page.stage === Wheelstar.Running
            text: i18n("The test is running, please, don't touch the wheel and wait.")
        }

        QQC2.BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: page.busy
            running: visible
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            visible: page.prompt
            type: Kirigami.MessageType.Warning
            text: i18n("WARNING: Don't touch the wheel and leave a clear space around it to avoid accidents or unreliable results.")
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.margins: Kirigami.Units.largeSpacing
            visible: page.prompt

            QQC2.Button {
                text: i18n("Back")
                icon.name: "go-previous"
                onClicked: Wheelstar.previousTest()
            }

            QQC2.Button {
                text: i18n("Run")
                icon.name: "media-playback-start"
                onClicked: Wheelstar.runTest()
            }
        }
    }

    FormCard.FormHeader {
        visible: page.stage === Wheelstar.Results
        title: i18n("Results")
    }

    FormCard.FormCard {
        visible: page.stage === Wheelstar.Results

        Repeater {
            model: [
                { key: "latency", text: i18n("Latency (ms):") },
                { key: "maxVelocity", text: i18n("Max. angular velocity (RPM):") },
                { key: "maxAcceleration", text: i18n("Max. angular acceleration (RPM/s):") },
                { key: "timeToMaxAcceleration", text: i18n("Time to max. angular acceleration (ms):") },
                { key: "meanAcceleration", text: i18n("Mean angular acceleration (RPM/s):") },
                { key: "maxDeceleration", text: i18n("Max. angular deceleration (RPM/s):") },
                { key: "timeToMaxDeceleration", text: i18n("Time to max. angular deceleration (ms):") },
                { key: "meanDeceleration", text: i18n("Mean angular deceleration (RPM/s):") },
                { key: "residualDeceleration", text: i18n("Residual angular deceleration (RPM/s):") },
                { key: "estimatedSnr", text: i18n("Estimated SNR (dB):") },
                { key: "minimumLevel", text: i18n("Min. force level (%):") }
            ]

            delegate: FormCard.FormTextDelegate {
                required property var modelData

                text: modelData.text
                trailing: QQC2.Label {
                    text: Wheelstar.testResults[modelData.key] ?? ""
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    FileDialog {
        id: importDialog

        title: i18n("CSV file to import")
        nameFilters: [i18n("CSV files (*.csv)")]
        onAccepted: Wheelstar.importTestReport(selectedFile)
    }

    FileDialog {
        id: exportDialog

        title: i18n("CSV file to export")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [i18n("CSV files (*.csv)")]
        onAccepted: Wheelstar.exportTestReport(selectedFile)
    }
}
