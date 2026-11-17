// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The drives the telemetry plugin reported, newest first: one row each, and a dialog with the details.
Kirigami.ScrollablePage {
    id: page

    /// The game the list is narrowed to; empty for every game.
    property string game: ""

    readonly property var games: {
        const names = [];

        for (const session of Wheelstar.sessions) {
            if (!names.includes(session.game)) {
                names.push(session.game);
            }
        }

        return names;
    }
    readonly property var shown: game === "" ? Wheelstar.sessions : Wheelstar.sessions.filter(session => session.game === game)
    readonly property real totalSeconds: shown.reduce((sum, session) => sum + session.drivingSeconds, 0)
    // A drive from before the odometer has no distance (null) and adds nothing.
    readonly property real totalKilometres: shown.reduce((sum, session) => sum + (session.distanceKilometres ?? 0), 0)

    function formatDuration(seconds: real): string {
        // Rounded to whole minutes first, so 59.6 minutes shows as "1 h 0 min" and not "60 min".
        const minutes = Math.round(seconds / 60);

        if (minutes < 60) {
            return i18n("%1 min", minutes);
        }

        return i18n("%1 h %2 min", Math.floor(minutes / 60), minutes % 60);
    }

    /// Start and end of a drive: "15.11.26 16:06–17:11", and the end with its date when it is on another day.
    function formatSpan(start: date, end: date): string {
        const sameDay = start.toDateString() === end.toDateString();
        const until = sameDay ? Qt.formatTime(end, Qt.locale().timeFormat(Locale.ShortFormat))
                              : Qt.formatDateTime(end, Qt.locale(), Locale.ShortFormat);
        return Qt.formatDateTime(start, Qt.locale(), Locale.ShortFormat) + "–" + until;
    }

    /// A distance in kilometres, or a dash for a drive recorded before the plugin sent the odometer.
    function formatKilometres(kilometres: var, decimals: int): string {
        return kilometres === null || kilometres === undefined ? "–" : i18n("%1 km", kilometres.toLocaleString(Qt.locale(), "f", decimals));
    }

    title: i18n("Statistics")

    actions: [
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            enabled: !Wheelstar.sessionsLoading
            onTriggered: Wheelstar.refreshSessions()
        }
    ]

    Component.onCompleted: Wheelstar.refreshSessions()

    ListView {
        id: list

        model: page.shown
        spacing: 0

        header: ColumnLayout {
            width: ListView.view.width
            spacing: Kirigami.Units.smallSpacing
            visible: list.count > 0

            RowLayout {
                Layout.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18np("%1 drive · %2 · %3 km", "%1 drives · %2 · %3 km", list.count,
                                page.formatDuration(page.totalSeconds), Math.round(page.totalKilometres))
                    font.bold: true
                    elide: Text.ElideRight
                }

                QQC2.ComboBox {
                    // Only worth choosing from when there is more than one game.
                    visible: page.games.length > 1
                    model: [i18n("All games")].concat(page.games)
                    onActivated: index => page.game = index === 0 ? "" : page.games[index - 1]
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }
        }

        delegate: QQC2.ItemDelegate {
            id: item

            required property var modelData

            width: ListView.view.width
            onClicked: details.show(modelData)

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: page.formatSpan(item.modelData.startedAt, item.modelData.endedAt)
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: item.modelData.game
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    horizontalAlignment: Text.AlignRight
                    text: page.formatDuration(item.modelData.drivingSeconds)
                    font.features: { "tnum": 1 }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    horizontalAlignment: Text.AlignRight
                    text: page.formatKilometres(item.modelData.distanceKilometres, 0)
                    font.features: { "tnum": 1 }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    horizontalAlignment: Text.AlignRight
                    text: i18n("%1 km/h", Math.round(item.modelData.averageKmh))
                    font.features: { "tnum": 1 }
                }
            }
        }

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            running: Wheelstar.sessionsLoading && list.count === 0
            visible: running
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0 && !Wheelstar.sessionsLoading
            icon.name: Wheelstar.sessionsError.length > 0 ? "network-disconnect" : "office-chart-bar"
            text: Wheelstar.sessionsError.length > 0 ? i18n("The statistics cannot be read") : i18n("No drives recorded yet")
            explanation: Wheelstar.sessionsError.length > 0
                         ? Wheelstar.sessionsError
                         : i18n("Install the telemetry plugin for Euro Truck Simulator 2 or American Truck Simulator, then drive: every drive of 30 seconds or more shows up here.")
        }
    }

    SessionDialog {
        id: details

        formatDuration: page.formatDuration
        formatSpan: page.formatSpan
        formatKilometres: page.formatKilometres
    }
}
