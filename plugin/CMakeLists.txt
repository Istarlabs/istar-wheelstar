# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

# The telemetry plugin the game loads. It is a separate project because it is built for Windows, with another
# compiler than the application:
#
#   cmake -S plugin -B build-plugin -DCMAKE_TOOLCHAIN_FILE=plugin/mingw-w64.cmake
#   cmake --build build-plugin
#
# The result, wheelstar_telemetry.dll, goes to "<game>/bin/win_x64/plugins/".
cmake_minimum_required(VERSION 3.20)
project(wheelstar-telemetry-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(wheelstar_telemetry SHARED wheelstar_telemetry.cpp)

set(SCS_SDK ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/scs-sdk/include)
target_include_directories(wheelstar_telemetry PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SCS_SDK}
    ${SCS_SDK}/common
    ${SCS_SDK}/amtrucks
    ${SCS_SDK}/eurotrucks2
)

# The game looks for the plain name, and a statically linked runtime keeps the DLL free of mingw's own libraries.
set_target_properties(wheelstar_telemetry PROPERTIES PREFIX "")
target_link_libraries(wheelstar_telemetry PRIVATE ws2_32 -static-libgcc -static-libstdc++ -static)
# Without the link time in the PE header, the same source gives the same bytes, so the package is only published
# again when the plugin really changed.
target_link_options(wheelstar_telemetry PRIVATE -Wl,--no-insert-timestamp)
target_compile_options(wheelstar_telemetry PRIVATE -Wall -Wextra)
