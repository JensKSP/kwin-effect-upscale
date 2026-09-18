# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
# The texts the settings page, the display and the logs all read. They need
# neither KWin nor a GPU, so they are checked on their own.
add_executable(
    upscale_snapshot_test
    snapshot_test.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/snapshot.cpp
)
target_include_directories(upscale_snapshot_test PRIVATE ../src/plugins/upscale)
target_link_libraries(upscale_snapshot_test PRIVATE kwin KF6::I18n Qt6::Test Libdrm::Libdrm)
add_test(NAME upscale-snapshot COMMAND upscale_snapshot_test)

add_executable(upscale_package_test package_test.cpp)
target_link_libraries(upscale_package_test PRIVATE Qt6::Core KF6::CoreAddons)
add_executable(upscale_resolution_test ../tools/upscale-resolution-test.cpp)
target_include_directories(upscale_resolution_test PRIVATE ../src/plugins/upscale)
# These assertions are test oracles, including in distribution release builds.
target_compile_options(upscale_resolution_test PRIVATE -UNDEBUG)
add_test(NAME upscale-resolution COMMAND upscale_resolution_test)
if(UPSCALE_FUZZING)
    add_executable(upscale_resolution_fuzz resolution_fuzz.cpp)
    target_include_directories(upscale_resolution_fuzz PRIVATE ../src/plugins/upscale)
    target_compile_options(upscale_resolution_fuzz PRIVATE -fsanitize=fuzzer)
    # libFuzzer supplies main; use its static sanitizer runtime so ASan is
    # initialized before the fuzzer's own allocation interceptors.
    target_link_options(upscale_resolution_fuzz PRIVATE -fsanitize=fuzzer -static-libsan)
endif()

enable_language(C)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(protocol_sources)
foreach(protocol IN ITEMS xdg-shell viewporter)
    set(protocol_xml "${WAYLAND_PROTOCOLS}/stable/${protocol}/${protocol}.xml")
    set(protocol_header "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h")
    set(protocol_code "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c")
    add_custom_command(
        OUTPUT ${protocol_header} ${protocol_code}
        COMMAND ${WAYLAND_SCANNER} client-header ${protocol_xml} ${protocol_header}
        COMMAND ${WAYLAND_SCANNER} private-code ${protocol_xml} ${protocol_code}
        DEPENDS ${protocol_xml}
        COMMENT "Generating ${protocol} client protocol"
        VERBATIM
    )
    list(APPEND protocol_sources ${protocol_header} ${protocol_code})
endforeach()
add_executable(upscale_integration_test integration_test.cpp wayland_client.cpp ${protocol_sources})
target_include_directories(upscale_integration_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
    upscale_integration_test
    PRIVATE Qt6::Test Qt6::Gui Qt6::DBus KF6::ConfigCore PkgConfig::WAYLAND_CLIENT
)
if(KWin_VERSION VERSION_LESS 6.7)
    set(driver_sources
        effect_driver.cpp
        ../src/plugins/upscale/display.cpp
        ../src/plugins/upscale/display.h
        ../src/plugins/upscale/eligibility.cpp
        ../src/plugins/upscale/overlay.cpp
        ../src/plugins/upscale/snapshot.cpp
        ../src/plugins/upscale/upscale.cpp
        ../src/plugins/upscale/upscale.h
        ../src/plugins/upscale/scaler.cpp
        ../src/plugins/upscale/upscale.qrc
    )
    kconfig_add_kcfg_files(driver_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
    add_library(upscale_test_driver MODULE ${driver_sources})
    target_include_directories(upscale_test_driver PRIVATE ../src/plugins/upscale)
    target_link_libraries(upscale_test_driver PRIVATE kwin KF6::ConfigGui KF6::I18n Libdrm::Libdrm)
    set_target_properties(
        upscale_test_driver
        PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/kwin/effects/plugins"
    )
    add_test(
        NAME upscale-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_integration_test>
    )
    set_tests_properties(upscale-integration PROPERTIES TIMEOUT 100)
    if(UPSCALE_SANITIZER)
        set_tests_properties(
            upscale-integration
            PROPERTIES
                ENVIRONMENT
                    "UPSCALE_SANITIZER_RUNTIME=${UPSCALE_SANITIZER_RUNTIME};LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=1"
        )
    endif()
endif()
set(render_test_SOURCES
    render_test.cpp
    ../src/plugins/upscale/display.cpp
    ../src/plugins/upscale/display.h
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/overlay.cpp
    ../src/plugins/upscale/scaler.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/upscale.qrc
)
kconfig_add_kcfg_files(render_test_SOURCES ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(upscale_render_test ${render_test_SOURCES})
target_include_directories(upscale_render_test PRIVATE ../src/plugins/upscale)
target_link_libraries(
    upscale_render_test
    PRIVATE kwin KF6::ConfigGui KF6::I18n Qt6::Test Libdrm::Libdrm
)
add_test(NAME upscale-render COMMAND upscale_render_test)
# The display reads the effect's configuration, which must be this test's own
# and never the one belonging to whoever runs it.
set_tests_properties(
    upscale-render
    PROPERTIES
        ENVIRONMENT
            "LIBGL_ALWAYS_SOFTWARE=1;QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-config"
)
if(KWin_VERSION VERSION_LESS 6.7)
    add_test(NAME upscale-render-gles COMMAND upscale_render_test)
    set_tests_properties(
        upscale-render-gles
        PROPERTIES
            ENVIRONMENT
                "LIBGL_ALWAYS_SOFTWARE=1;KWIN_COMPOSE=O2ES;QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-config"
    )
endif()

if(KWIN_BUILD_KCMS)
    set(config_test_SOURCES
        config_test.cpp
        ../src/plugins/upscale/supportinformation.cpp
        ../src/plugins/upscale/upscale_config.cpp
    )
    kconfig_add_kcfg_files(config_test_SOURCES ../src/plugins/upscale/upscaleconfig.kcfgc)
    add_executable(upscale_config_test ${config_test_SOURCES})
    target_include_directories(upscale_config_test PRIVATE ../src/plugins/upscale)
    target_link_libraries(
        upscale_config_test
        PRIVATE KF6::KCMUtils KF6::ConfigGui KF6::CoreAddons KF6::I18n Qt6::DBus Qt6::Test
    )
    add_test(NAME upscale-config COMMAND upscale_config_test)
    set_tests_properties(upscale-config PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
