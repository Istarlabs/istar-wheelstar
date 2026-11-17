# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(istar-wheelstar VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.8.0)
set(KF_MIN_VERSION 6.8.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Deliberately no include(KDEClangFormat): it rewrites .clang-format at every configure with ECM's copy.

# KDECompilerSettings pins the standard to 17, so this has to come after it.
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# KDECompilerSettings turns exceptions off; std::jthread and the standard containers expect them.
kde_enable_exceptions()

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Concurrent Gui Network Sql Widgets Qml Quick QuickControls2 Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    I18n
    CoreAddons
    Config
    Crash
    DBusAddons
    GlobalAccel
    Notifications
    StatusNotifierItem
    WindowSystem
    Kirigami
)

# Loaded only at run time (the QML modules), but a build that cannot find it would produce a program whose window fails.
find_package(KF6KirigamiAddons REQUIRED)
find_package(KF6QuickCharts ${KF_MIN_VERSION} REQUIRED)

# Profiles and preferences are TOML.
find_package(tomlplusplus 3 REQUIRED)

# The games encrypt their save files with AES; OpenSSL's libcrypto decrypts them.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)

add_subdirectory(src)
add_subdirectory(data)

ki18n_install(po)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
