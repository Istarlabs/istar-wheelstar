// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import net.kurokyuu.wheelstar

/// The jobs, delivered and given up, from the telemetry and from the save game: filtered, sorted, or summed up
/// per route.
Kirigami.ScrollablePage {
    id: page

    /// "jobs" or "routes".
    property string view: "jobs"
    /// The key of the sorting of each view, and whether the largest (or latest) comes first.
    property string jobSort: "date"
    property string routeSort: "count"
    property bool descending: true
    /// A route chosen in the routes view, { from, to }, or null for every route.
    property var route: null

    readonly property var jobSorts: [
        { key: "date", text: i18nc("@item sort jobs by", "Date") },
        { key: "distance", text: i18nc("@item sort jobs by", "Distance") },
        { key: "revenue", text: i18nc("@item sort jobs by", "Revenue") },
        { key: "experience", text: i18nc("@item sort jobs by", "Experience") }
    ]
    readonly property var routeSorts: [
        { key: "count", text: i18nc("@item sort routes by", "Times driven") },
        { key: "date", text: i18nc("@item sort routes by", "Last driven") },
        { key: "distance", text: i18nc("@item sort routes by", "Distance") },
        { key: "revenue", text: i18nc("@item sort routes by", "Revenue") }
    ]

    /// The jobs that pass the filters, unsorted.
    readonly property var filtered: {
        const vehicle = vehicleFilter.currentValue;
        const since = page.periodStart(periodFilter.currentValue);
        const search = searchField.text.trim().toLocaleLowerCase();

        return Wheelstar.jobs.filter(job => {
            if ((vehicle === "truck" && job.bus) || (vehicle === "bus" && !job.bus)) {
                return false;
            }

            // A job from the save game has no real date; only "All time" shows it.
            if (since && (!job.finishedAt || job.finishedAt < since)) {
                return false;
            }

            if (page.route && (job.from !== page.route.from || job.to !== page.route.to)) {
                return false;
            }

            return search === "" || [job.from, job.to, job.fromCompany, job.toCompany, job.cargo, job.truck]
                .some(text => (text ?? "").toLocaleLowerCase().includes(search));
        });
    }
    readonly property var sortedJobs: page.sorted(page.filtered, page.jobSort, page.jobValue)
    readonly property var routes: page.sorted(page.routesOf(page.filtered), page.routeSort, page.routeValue)
    readonly property var delivered: page.filtered.filter(job => job.delivered)
    readonly property int hiddenWithoutDate: periodFilter.currentValue === "all"
        ? 0 : Wheelstar.jobs.filter(job => !job.finishedAt).length

    /// The first moment of a period, or null for all time.
    function periodStart(period: string): var {
        const now = new Date();

        switch (period) {
        case "today":
            return new Date(now.getFullYear(), now.getMonth(), now.getDate());
        case "week":
            return new Date(now.getTime() - 7 * 24 * 3600 * 1000);
        case "month":
            return new Date(now.getTime() - 30 * 24 * 3600 * 1000);
        case "year":
            return new Date(now.getFullYear(), 0, 1);
        default:
            return null;
        }
    }

    function kilometres(job: var): real {
        return job.drivenKilometres || job.plannedKilometres || 0;
    }

    function jobValue(job: var, key: string): real {
        switch (key) {
        case "distance":
            return page.kilometres(job);
        case "revenue":
            return job.revenue;
        case "experience":
            return job.experience;
        default:
            // The game's clock orders a job from the save game as well as one from the telemetry.
            return job.gameTime;
        }
    }

    function routeValue(route: var, key: string): real {
        switch (key) {
        case "date":
            return route.lastGameTime;
        case "distance":
            return route.kilometres;
        case "revenue":
            return route.revenue;
        default:
            return route.count;
        }
    }

    function sorted(items: var, key: string, value: var): var {
        const direction = page.descending ? -1 : 1;
        return items.slice().sort((a, b) => direction * (value(a, key) - value(b, key)));
    }

    /// The delivered jobs summed up per route, from → to.
    function routesOf(jobs: var): var {
        const byRoute = {};

        for (const job of jobs) {
            if (!job.delivered) {
                continue;
            }

            const key = job.from + "\u0000" + job.to;
            const route = byRoute[key] ?? (byRoute[key] = {
                from: job.from, to: job.to, bus: job.bus, count: 0, kilometres: 0, revenue: 0,
                lastGameTime: 0, currency: job.currency
            });
            route.count += 1;
            route.kilometres += page.kilometres(job);
            route.revenue += job.revenue;
            route.lastGameTime = Math.max(route.lastGameTime, job.gameTime);
        }

        return Object.values(byRoute);
    }

    function formatMoney(amount: real, currency: string): string {
        return Number(amount).toLocaleString(Qt.locale(), "f", 0) + " " + currency;
    }

    function formatKilometres(kilometres: real): string {
        // i18n: %1 a distance in kilometres
        return i18n("%1 km", Math.round(kilometres).toLocaleString(Qt.locale(), "f", 0));
    }

    /// When a job ended: the real date when the telemetry saw it, else the game's day and time.
    function formatWhen(job: var): string {
        if (job.finishedAt) {
            return Qt.formatDateTime(job.finishedAt, Qt.locale(), Locale.ShortFormat);
        }

        // i18n: %1 the in-game day, %2 the in-game time
        return i18n("Day %1, %2", job.gameDay, Qt.formatTime(job.gameClock, Qt.locale().timeFormat(Locale.ShortFormat)));
    }

    /// What a job carried: the cargo, or a bus's passengers.
    function formatLoad(job: var): string {
        return job.bus
            // i18n: a bus job; %1 the passengers over all its stops
            ? i18np("Bus · %1 passenger", "Bus · %1 passengers", job.passengers) + " · "
              // i18n: the stops of a bus job
              + i18np("%1 stop", "%1 stops", job.stops)
            : job.cargo;
    }

    title: i18n("Jobs")

    actions: [
        Kirigami.Action {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            enabled: !Wheelstar.sessionsLoading
            onTriggered: Wheelstar.refreshSessions()
        }
    ]

    Component.onCompleted: Wheelstar.refreshSessions()

    header: QQC2.Control {
        padding: Kirigami.Units.largeSpacing

        background: Rectangle {
            color: Kirigami.Theme.backgroundColor

            Kirigami.Separator {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true

                Kirigami.SearchField {
                    id: searchField

                    Layout.fillWidth: true
                    placeholderText: i18n("City, company, cargo or vehicle…")
                }

                QQC2.ComboBox {
                    id: vehicleFilter

                    textRole: "text"
                    valueRole: "key"
                    model: [
                        { key: "all", text: i18nc("@item filter jobs", "All vehicles") },
                        { key: "truck", text: i18nc("@item filter jobs", "Trucks") },
                        { key: "bus", text: i18nc("@item filter jobs", "Buses") }
                    ]
                }

                QQC2.ComboBox {
                    id: periodFilter

                    textRole: "text"
                    valueRole: "key"
                    model: [
                        { key: "all", text: i18nc("@item filter jobs", "All time") },
                        { key: "today", text: i18nc("@item filter jobs", "Today") },
                        { key: "week", text: i18nc("@item filter jobs", "Last 7 days") },
                        { key: "month", text: i18nc("@item filter jobs", "Last 30 days") },
                        { key: "year", text: i18nc("@item filter jobs", "This year") }
                    ]
                }
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.TabBar {
                    id: viewBar

                    currentIndex: page.view === "routes" ? 1 : 0

                    QQC2.TabButton {
                        text: i18n("Jobs")
                        width: implicitWidth
                        onClicked: page.view = "jobs"
                    }

                    QQC2.TabButton {
                        text: i18n("Routes")
                        width: implicitWidth
                        onClicked: page.view = "routes"
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    text: i18n("Sort by:")
                }

                QQC2.ComboBox {
                    id: sortBox

                    readonly property var sorts: page.view === "routes" ? page.routeSorts : page.jobSorts

                    textRole: "text"
                    valueRole: "key"
                    model: sorts
                    currentIndex: Math.max(0, sorts.findIndex(sort => sort.key === (page.view === "routes" ? page.routeSort : page.jobSort)))
                    onActivated: {
                        if (page.view === "routes") {
                            page.routeSort = currentValue;
                        } else {
                            page.jobSort = currentValue;
                        }
                    }
                }

                QQC2.ToolButton {
                    icon.name: page.descending ? "view-sort-descending" : "view-sort-ascending"
                    text: page.descending ? i18n("Descending") : i18n("Ascending")
                    display: QQC2.AbstractButton.IconOnly
                    onClicked: page.descending = !page.descending

                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: page.route !== null
                type: Kirigami.MessageType.Information
                // i18n: %1 the city the route starts in, %2 the one it goes to
                text: page.route ? i18n("Only the route %1 → %2", page.route.from, page.route.to) : ""
                actions: [
                    Kirigami.Action {
                        text: i18n("All routes")
                        icon.name: "edit-clear"
                        onTriggered: page.route = null
                    }
                ]
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: page.hiddenWithoutDate > 0
                type: Kirigami.MessageType.Information
                text: i18np("One job from the save game has no real date and only shows with “All time”.",
                            "%1 jobs from the save game have no real date and only show with “All time”.",
                            page.hiddenWithoutDate)
            }

            QQC2.Label {
                Layout.fillWidth: true
                visible: page.filtered.length > 0
                // i18n: %1 the number of delivered jobs, %2 the kilometres, %3 the money earned with its currency
                text: i18np("%1 delivery · %2 · %3", "%1 deliveries · %2 · %3", page.delivered.length,
                            page.formatKilometres(page.delivered.reduce((sum, job) => sum + page.kilometres(job), 0)),
                            page.formatMoney(page.delivered.reduce((sum, job) => sum + job.revenue, 0),
                                             page.delivered.length > 0 ? page.delivered[0].currency : "€"))
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }

    ListView {
        id: list

        model: page.view === "routes" ? page.routes : page.sortedJobs
        spacing: 0

        delegate: page.view === "routes" ? routeDelegate : jobDelegate

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: list.count === 0 && !Wheelstar.sessionsLoading
            icon.name: "view-list-details"
            text: Wheelstar.jobs.length === 0 ? i18n("No jobs recorded yet") : i18n("No job matches the filter")
            explanation: Wheelstar.jobs.length === 0
                         ? i18n("Jobs show up here when you deliver them with the telemetry plugin installed, or when you import them from the save game on the Career page.")
                         : ""
        }
    }

    Component {
        id: jobDelegate

        QQC2.ItemDelegate {
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
                        // i18n: %1 the city the job starts in, %2 the one it goes to
                        text: i18n("%1 → %2", item.modelData.from, item.modelData.to)
                        font.bold: true
                        font.strikeout: !item.modelData.delivered
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: page.formatLoad(item.modelData) + " · " + page.formatWhen(item.modelData)
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    horizontalAlignment: Text.AlignRight
                    text: page.formatKilometres(page.kilometres(item.modelData))
                    font.features: { "tnum": 1 }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                    horizontalAlignment: Text.AlignRight
                    text: page.formatMoney(item.modelData.revenue, item.modelData.currency)
                    color: item.modelData.revenue < 0 ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    Component {
        id: routeDelegate

        QQC2.ItemDelegate {
            id: routeItem

            required property var modelData

            width: ListView.view.width
            onClicked: {
                page.route = { from: modelData.from, to: modelData.to };
                page.view = "jobs";
            }

            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("%1 → %2", routeItem.modelData.from, routeItem.modelData.to)
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        // i18n: how often a route was driven; with "Bus" in front for a bus route
                        text: (routeItem.modelData.bus ? i18n("Bus") + " · " : "")
                              + i18np("driven once", "driven %1 times", routeItem.modelData.count)
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                    horizontalAlignment: Text.AlignRight
                    text: page.formatKilometres(routeItem.modelData.kilometres)
                    font.features: { "tnum": 1 }
                }

                QQC2.Label {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                    horizontalAlignment: Text.AlignRight
                    text: page.formatMoney(routeItem.modelData.revenue, routeItem.modelData.currency)
                    font.features: { "tnum": 1 }
                }
            }
        }
    }

    JobDialog {
        id: details

        formatMoney: page.formatMoney
        formatWhen: page.formatWhen
    }
}
