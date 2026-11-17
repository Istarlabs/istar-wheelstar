// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

Kirigami.ScrollablePage {
    id: page

    title: i18n("Profiles")

    actions: [
        Kirigami.Action {
            text: i18n("New")
            icon.name: "document-new"
            enabled: Wheelstar.hasDevice
            onTriggered: nameDialog.ask("", i18n("New profile"))
        },
        Kirigami.Action {
            text: i18n("Import")
            icon.name: "document-import"
            onTriggered: importDialog.open()
        }
    ]

    ListView {
        id: list

        model: Wheelstar.profiles

        delegate: QQC2.ItemDelegate {
            id: item

            required property string modelData

            width: ListView.view.width
            highlighted: modelData === Wheelstar.currentProfile
            onClicked: Wheelstar.loadProfile(modelData)

            contentItem: RowLayout {
                Kirigami.Icon {
                    source: "document-properties"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: item.modelData
                    elide: Text.ElideRight
                }

                QQC2.ToolButton {
                    icon.name: "edit-rename"
                    text: i18n("Rename")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: nameDialog.ask(item.modelData, i18n("Rename"))
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    icon.name: "document-export"
                    text: i18n("Export")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        exportDialog.profile = item.modelData;
                        exportDialog.selectedFile = item.modelData + ".toml";
                        exportDialog.open();
                    }
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }

                QQC2.ToolButton {
                    icon.name: "edit-delete"
                    text: i18n("Delete")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: {
                        deleteDialog.profile = item.modelData;
                        deleteDialog.open();
                    }
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
            }
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0
            icon.name: "document-multiple"
            text: i18n("No profiles yet")
        }
    }

    Kirigami.PromptDialog {
        id: nameDialog

        property string original

        function ask(name: string, heading: string): void {
            original = name;
            title = heading;
            nameField.text = name;
            open();
            nameField.forceActiveFocus();
        }

        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            const name = nameField.text.trim();

            if (original.length > 0) {
                Wheelstar.renameProfile(original, name);
            } else if (Wheelstar.profileExists(name)) {
                overwriteDialog.profile = name;
                overwriteDialog.open();
            } else {
                Wheelstar.createProfile(name);
            }
        }

        QQC2.TextField {
            id: nameField

            placeholderText: i18n("Profile name")
            onAccepted: nameDialog.accept()
        }
    }

    Kirigami.PromptDialog {
        id: overwriteDialog

        property string profile

        title: i18n("This profile already exists. Are you sure?")
        subtitle: profile
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        onAccepted: Wheelstar.createProfile(profile)
    }

    Kirigami.PromptDialog {
        id: deleteDialog

        property string profile

        title: i18n("This profile will be deleted, are you sure?")
        subtitle: profile
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        onAccepted: Wheelstar.deleteProfile(profile)
    }

    FileDialog {
        id: importDialog

        title: i18n("Choose profile file to import")
        nameFilters: [i18n("Profiles (*.toml *.ini)")]
        onAccepted: Wheelstar.importProfile(selectedFile)
    }

    FileDialog {
        id: exportDialog

        property string profile

        title: i18n("Export profile to")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "toml"
        nameFilters: [i18n("Profiles (*.toml)")]
        onAccepted: Wheelstar.exportProfile(profile, selectedFile)
    }
}
