# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(test gamedatatest jobtrackertest savegametest sessionaccumulatortest sessionstoretest telemetrypackettest testanalysistest wheelsettingstest)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE wheelstar-core Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

add_test(NAME ets2plugintest
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/ets2plugintest.sh ${PROJECT_SOURCE_DIR}/data/ets2-plugin/istar-wheelstar-ets2-plugin)
