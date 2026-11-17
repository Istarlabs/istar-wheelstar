// SPDX-FileCopyrightText: 2026 Pascal Klose
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/// One job in detail.
Kirigami.Dialog {
    id: dialog

    /// A job as the controller hands it out.
    property var job: ({})
    property var formatMoney
    property var formatWhen

    function show(data: var): void {
        job = data;
        open();
    }

    // i18n: %1 the city the job starts in, %2 the one it goes to
    title: i18n("%1 → %2", job.from ?? "", job.to ?? "")
    standardButtons: Kirigami.Dialog.Close
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing

    Kirigami.FormLayout {
        QQC2.Label {
            Kirigami.FormData.label: i18n("From:")
            // i18n: %1 the company, %2 the city
            text: dialog.job.bus ? (dialog.job.from ?? "") : i18n("%1, %2", dialog.job.fromCompany ?? "", dialog.job.from ?? "")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("To:")
            text: dialog.job.bus ? (dialog.job.to ?? "") : i18n("%1, %2", dialog.job.toCompany ?? "", dialog.job.to ?? "")
        }

        QQC2.Label {
            visible: dialog.job.bus === true
            Kirigami.FormData.label: i18n("Passengers:")
            // i18n: %1 the passengers over all stops, %2 the stops
            text: i18n("%1 at %2 stops", dialog.job.passengers ?? 0, dialog.job.stops ?? 0)
        }

        QQC2.Label {
            visible: dialog.job.bus === true
            Kirigami.FormData.label: i18n("Satisfaction:")
            text: i18n("%1 %", Math.round((dialog.job.satisfaction ?? 0) * 100))
        }

        QQC2.Label {
            visible: dialog.job.bus !== true
            Kirigami.FormData.label: i18n("Cargo:")
            text: (dialog.job.tonnes ?? 0) > 0
                  // i18n: %1 the cargo, %2 its mass in tonnes
                  ? i18n("%1, %2 t", dialog.job.cargo, dialog.job.tonnes.toLocaleString(Qt.locale(), "f", 1))
                  : (dialog.job.cargo ?? "")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Finished:")
            text: dialog.formatWhen && dialog.job.gameDay ? dialog.formatWhen(dialog.job) : ""
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Status:")
            text: dialog.job.delivered ? i18n("Delivered") : i18n("Cancelled")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Distance:")
            text: (dialog.job.plannedKilometres ?? 0) > 0 && (dialog.job.drivenKilometres ?? 0) > 0
                  // i18n: %1 the kilometres driven, %2 the kilometres the job was planned for
                  ? i18n("%1 km (planned %2 km)", Math.round(dialog.job.drivenKilometres), Math.round(dialog.job.plannedKilometres))
                  : i18n("%1 km", Math.round(dialog.job.drivenKilometres || dialog.job.plannedKilometres || 0))
        }

        QQC2.Label {
            Kirigami.FormData.label: dialog.job.delivered ? i18n("Revenue:") : i18n("Penalty:")
            text: dialog.formatMoney ? dialog.formatMoney(Math.abs(dialog.job.revenue ?? 0), dialog.job.currency ?? "") : ""
        }

        QQC2.Label {
            visible: (dialog.job.fines ?? 0) > 0
            Kirigami.FormData.label: i18n("Fines:")
            text: dialog.formatMoney ? dialog.formatMoney(dialog.job.fines ?? 0, dialog.job.currency ?? "") : ""
        }

        QQC2.Label {
            visible: dialog.job.delivered === true
            Kirigami.FormData.label: i18n("Experience:")
            text: i18n("%1 XP", dialog.job.experience ?? 0)
        }

        QQC2.Label {
            visible: dialog.job.delivered === true && dialog.job.bus !== true
            Kirigami.FormData.label: i18n("Cargo damage:")
            text: i18n("%1 %", ((dialog.job.cargoDamage ?? 0) * 100).toLocaleString(Qt.locale(), "f", 1))
        }

        QQC2.Label {
            visible: (dialog.job.deliveryMinutes ?? 0) > 0
            Kirigami.FormData.label: i18n("Time in the game:")
            // i18n: in-game hours and minutes the job took
            text: i18n("%1 h %2 min", Math.floor((dialog.job.deliveryMinutes ?? 0) / 60), (dialog.job.deliveryMinutes ?? 0) % 60)
        }

        QQC2.Label {
            visible: (dialog.job.market ?? "") !== ""
            Kirigami.FormData.label: i18n("Job market:")
            text: dialog.job.market ?? ""
        }

        QQC2.Label {
            visible: (dialog.job.truck ?? "") !== ""
            Kirigami.FormData.label: dialog.job.bus ? i18n("Bus:") : i18n("Truck:")
            text: dialog.job.truck ?? ""
        }

        QQC2.Label {
            visible: dialog.job.autoParked === true || dialog.job.autoLoaded === true || dialog.job.special === true
            Kirigami.FormData.label: i18n("Notes:")
            text: [dialog.job.special ? i18n("special transport") : "",
                   dialog.job.autoParked ? i18n("parked automatically") : "",
                   dialog.job.autoLoaded ? i18n("loaded automatically") : ""].filter(note => note !== "").join(", ")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Recorded from:")
            text: dialog.job.fromSaveGame ? i18n("the save game") : i18n("the telemetry")
        }
    }
}
