// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.config as KConfig
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import net.kurokyuu.wheelstar

Kirigami.ApplicationWindow {
    id: root

    title: i18n("Wheelstar")
    width: Kirigami.Units.gridUnit * 48
    height: Kirigami.Units.gridUnit * 42
    minimumWidth: Kirigami.Units.gridUnit * 26
    minimumHeight: Kirigami.Units.gridUnit * 20

    // Size and position are remembered in istar-wheelstarrc, like any KDE window.
    KConfig.WindowStateSaver {
        configGroupName: "MainWindow"
    }

    property string currentPage: "wheel"

    function showPage(name: string, component: Component): void {
        currentPage = name;
        pageStack.clear();
        pageStack.push(component);
    }

    globalDrawer: Kirigami.GlobalDrawer {
        modal: !root.wideScreen
        collapsible: true
        collapsed: false
        showHeaderWhenCollapsed: true

        actions: [
            Kirigami.Action {
                text: i18n("Controls")
                icon.name: "input-gaming"
                checkable: true
                checked: root.currentPage === "wheel"
                onTriggered: root.showPage("wheel", wheelPage)
            },
            Kirigami.Action {
                text: i18n("Force Feedback")
                icon.name: "transform-move"
                checkable: true
                checked: root.currentPage === "ffb"
                onTriggered: root.showPage("ffb", forceFeedbackPage)
            },
            Kirigami.Action {
                text: i18n("Tools")
                icon.name: "tools"
                checkable: true
                checked: root.currentPage === "tools"
                onTriggered: root.showPage("tools", toolsPage)
            },
            Kirigami.Action {
                text: i18n("Tests")
                icon.name: "office-chart-line"
                checkable: true
                checked: root.currentPage === "tests"
                onTriggered: root.showPage("tests", testsPage)
            },
            Kirigami.Action {
                text: i18n("Profiles")
                icon.name: "document-multiple"
                checkable: true
                checked: root.currentPage === "profiles"
                onTriggered: root.showPage("profiles", profilesPage)
            },
            // The pages about Euro Truck Simulator 2 and American Truck Simulator: a section of their own, there only
            // with the plugin, which is a package of its own.
            Kirigami.Action {
                separator: true
                visible: Wheelstar.telemetryPluginInstalled
            },
            Kirigami.Action {
                text: i18nc("@title the section of the pages about the games", "Truck Simulator")
                icon.name: "applications-games"
                enabled: false
                visible: Wheelstar.telemetryPluginInstalled
            },
            Kirigami.Action {
                text: i18n("Statistics")
                icon.name: "office-chart-bar"
                visible: Wheelstar.telemetryPluginInstalled
                checkable: true
                checked: root.currentPage === "statistics"
                onTriggered: root.showPage("statistics", statisticsPage)
            },
            Kirigami.Action {
                text: i18n("Jobs")
                icon.name: "view-list-details"
                visible: Wheelstar.telemetryPluginInstalled
                checkable: true
                checked: root.currentPage === "jobs"
                onTriggered: root.showPage("jobs", jobsPage)
            },
            Kirigami.Action {
                text: i18n("Career")
                icon.name: "trophy"
                visible: Wheelstar.telemetryPluginInstalled
                checkable: true
                checked: root.currentPage === "career"
                onTriggered: root.showPage("career", careerPage)
            },
            Kirigami.Action {
                text: i18n("Company")
                icon.name: "office-building"
                visible: Wheelstar.telemetryPluginInstalled
                checkable: true
                checked: root.currentPage === "company"
                onTriggered: root.showPage("company", companyPage)
            },
            Kirigami.Action {
                separator: true
            },
            Kirigami.Action {
                text: i18n("Preferences")
                icon.name: "settings-configure"
                checkable: true
                checked: root.currentPage === "settings"
                onTriggered: root.showPage("settings", settingsPage)
            },
            Kirigami.Action {
                text: i18n("About Wheelstar")
                icon.name: "help-about"
                checkable: true
                checked: root.currentPage === "about"
                onTriggered: root.showPage("about", aboutPage)
            }
        ]
    }

    header: ColumnLayout {
        spacing: 0

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: !Wheelstar.hasDevice
            type: Kirigami.MessageType.Information
            text: i18n("No device available.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Wheelstar.permissionsMissing
            type: Kirigami.MessageType.Warning
            text: i18n("You don't have the required permissions to change your wheel settings.")
            actions: Kirigami.Action {
                text: i18n("Install permission rules")
                icon.name: "dialog-password"
                onTriggered: Wheelstar.installPermissionRules()
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            visible: Wheelstar.commandPending
            type: Kirigami.MessageType.Positive
            text: i18n("The companion application is ready to start.")
            actions: Kirigami.Action {
                text: i18n("Start Application")
                icon.name: "media-playback-start"
                onTriggered: Wheelstar.startCommand()
            }
        }
    }

    Component.onCompleted: showPage("wheel", wheelPage)
    pageStack.globalToolBar.showNavigationButtons: Kirigami.ApplicationHeaderStyle.NoNavigationButtons

    Component {
        id: wheelPage
        WheelPage {}
    }

    Component {
        id: forceFeedbackPage
        ForceFeedbackPage {}
    }

    Component {
        id: toolsPage
        ToolsPage {}
    }

    Component {
        id: testsPage
        TestsPage {
            onOpenChart: chartWindow.show()
        }
    }

    Component {
        id: profilesPage
        ProfilesPage {}
    }

    Component {
        id: statisticsPage
        StatisticsPage {}
    }

    Component {
        id: jobsPage
        JobsPage {}
    }

    Component {
        id: careerPage
        CareerPage {}
    }

    Component {
        id: companyPage
        CompanyPage {}
    }

    Component {
        id: settingsPage
        SettingsPage {}
    }

    Component {
        id: aboutPage
        FormCard.AboutPage {}
    }

    OverlayWindow {
        id: overlay
    }

    ChartWindow {
        id: chartWindow
    }

    Kirigami.PromptDialog {
        id: messageDialog

        standardButtons: Kirigami.Dialog.Ok
    }

    Kirigami.PromptDialog {
        id: permissionDialog

        property string rulesFile
        property string targetDirectory

        title: i18n("You don't have the required permissions to change your wheel settings.")
        subtitle: i18n("You can fix it yourself by copying the files in %1 to the %2 directory and rebooting.",
                       rulesFile, targetDirectory) + "\n\n" + i18n("Do you want us to make this change for you?")
        standardButtons: Kirigami.Dialog.Yes | Kirigami.Dialog.No
        onAccepted: Wheelstar.installPermissionRules()
    }

    Connections {
        target: Wheelstar

        function onMessage(title: string, text: string, error: bool): void {
            if (!error) {
                root.showPassiveNotification(text.length > 0 ? title + " " + text : title);
                return;
            }

            messageDialog.title = title;
            messageDialog.subtitle = text;
            messageDialog.open();
        }

        function onPermissionsNeeded(rulesFile: string, targetDirectory: string): void {
            permissionDialog.rulesFile = rulesFile;
            permissionDialog.targetDirectory = targetDirectory;
            permissionDialog.open();
        }
    }
}
