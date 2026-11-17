# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

# The program without main(), so the tests link the same code.
add_library(wheelstar-core STATIC
    core/inifile.cpp
    core/inifile.h
    core/logging.cpp
    core/logging.h
    device/devicemanager.cpp
    device/devicemanager.h
    device/wheeldevice.cpp
    device/wheeldevice.h
    device/wheelmodels.h
    hardwaretest/ffbtest.cpp
    hardwaretest/ffbtest.h
    hardwaretest/testanalysis.cpp
    hardwaretest/testanalysis.h
    savegame/saveimport.cpp
    savegame/saveimport.h
    settings/buttonlabelstoml.h
    settings/buttonnames.cpp
    settings/buttonnames.h
    settings/wheelsettings.cpp
    settings/wheelsettings.h
    statistics/gameeventrecord.cpp
    statistics/gameeventrecord.h
    statistics/jobrecord.cpp
    statistics/jobrecord.h
    statistics/jobtracker.cpp
    statistics/jobtracker.h
    statistics/sessionaccumulator.cpp
    statistics/sessionaccumulator.h
    statistics/sessionrecorder.cpp
    statistics/sessionrecorder.h
    statistics/sessionstore.cpp
    statistics/sessionstore.h
    statistics/sessionsummary.cpp
    statistics/sessionsummary.h
    telemetry/telemetryreceiver.cpp
    telemetry/telemetryreceiver.h
)

# The project root too: the receiver includes the packet layout it shares with the plugin ("plugin/telemetry_packet.h").
target_include_directories(wheelstar-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(wheelstar-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
    KF6::I18n
    PkgConfig::UDEV
    tomlplusplus::tomlplusplus
    IstarTruckData::TruckData
)

add_executable(istar-wheelstar
    main.cpp
)

qt_policy(SET QTP0001 NEW)
qt_add_qml_module(istar-wheelstar
    URI net.kurokyuu.wheelstar
    VERSION 1.0
    SOURCES
        ui/controller.cpp
        ui/controller.h
    QML_FILES
        qml/Main.qml
        qml/WheelPage.qml
        qml/ForceFeedbackPage.qml
        qml/ToolsPage.qml
        qml/TestsPage.qml
        qml/ProfilesPage.qml
        qml/StatisticsPage.qml
        qml/SessionDialog.qml
        qml/JobsPage.qml
        qml/JobDialog.qml
        qml/CareerPage.qml
        qml/SettingsPage.qml
        qml/InputPanel.qml
        qml/SteeringWheel.qml
        qml/SliderDelegate.qml
        qml/OverlayWindow.qml
        qml/ChartWindow.qml
        qml/TestChart.qml
)

# qmltyperegistrar includes a registered class's header by its bare file name, so its folder has to be an include
# directory too; the sources themselves include it as "ui/controller.h".
target_include_directories(istar-wheelstar PRIVATE ui)

# WheelstarSettings, the application preferences.
kconfig_add_kcfg_files(istar-wheelstar wheelstarsettings.kcfgc)

target_compile_definitions(istar-wheelstar PRIVATE
    WHEELSTAR_VERSION="${PROJECT_VERSION}"
    WHEELSTAR_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

target_link_libraries(istar-wheelstar PRIVATE
    wheelstar-core
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18nQml
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::Crash
    KF6::DBusAddons
    KF6::GlobalAccel
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::WindowSystem
)

install(TARGETS istar-wheelstar ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
