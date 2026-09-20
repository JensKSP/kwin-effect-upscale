# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
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
        ../src/plugins/upscale/application.cpp
        ../src/plugins/upscale/display.cpp
        ../src/plugins/upscale/framestatistics.cpp
        ../src/plugins/upscale/framestatistics.cpp
        ../src/plugins/upscale/display.h
        ../src/plugins/upscale/eligibility.cpp
        ../src/plugins/upscale/refusaltext.cpp
        ../src/plugins/upscale/modeoverride.cpp
        ../src/plugins/upscale/observation.cpp
        ../src/plugins/upscale/overlay.cpp
        ../src/plugins/upscale/placement.cpp
        ../src/plugins/upscale/snapshot.cpp
        ../src/plugins/upscale/snapshot_metrics.cpp
        ../src/plugins/upscale/upscale.cpp
        ../src/plugins/upscale/x11geometry.cpp
        ../src/plugins/upscale/x11input.cpp
        ../src/plugins/upscale/x11resolution.cpp
        ../src/plugins/upscale/x11resolution_events.cpp
        ../src/plugins/upscale/x11resolution_present.cpp
        ../src/plugins/upscale/x11resolution_validate.cpp
        ../src/plugins/upscale/upscale.h
        ../src/plugins/upscale/scaler.cpp
        ../src/plugins/upscale/upscale.qrc
    )
    kconfig_add_kcfg_files(driver_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
    add_library(upscale_test_driver MODULE ${driver_sources})
    target_include_directories(upscale_test_driver PRIVATE ../src/plugins/upscale)
    target_link_libraries(
        upscale_test_driver
        PRIVATE kwin KF6::ConfigGui KF6::I18n Libdrm::Libdrm XCB::XCB XCB::RANDR
    )
    set_target_properties(
        upscale_test_driver
        PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/kwin/effects/plugins"
    )
    # The two cases below each start a nested kwin_wayland with Xwayland and
    # drive a real client through it. Whether that client answers inside the
    # effect's three second validation window is a property of the machine, so
    # they belong where a machine is chosen - the pull request checks, which run
    # the whole suite on both architectures - and not in a distribution package
    # build, which runs on whatever the builder has. debian/rules turns this off
    # for exactly that reason; everywhere else it stays on.
    option(UPSCALE_SESSION_TESTS "Register the tests that need a compositor session" ON)
    add_test(
        NAME upscale-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_integration_test>
    )
    # Each of these starts a KWin session of its own, with Xwayland, software
    # rendering and a client that has to answer resize requests inside the
    # plugin's own validation windows. Run beside other tests - a package
    # build runs four at once - they compete for the cores that decide how
    # fast those round trips are, and the waits in them stop describing the
    # plugin and start describing the machine. Observed 2026-09-20 on the
    # nightly's package jobs: waits satisfied in under a second here needed
    # 18 and 30 seconds there, on both architectures, while the same tests
    # passed in the quality jobs that run them alone. RUN_SERIAL is what says
    # "not beside anything else"; the timeout then covers the whole run of a
    # session that is merely slow rather than stuck.
    # The outer bound has to exceed the inner one, or ctest kills the session
    # before the harness can end it cleanly and say why - which is exactly the
    # confusion that cost most of a night. run-integration-test.py allows a
    # session 600 s, and 900 s under instrumentation; these sit above both.
    set_tests_properties(upscale-integration PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    add_executable(upscale_x11_integration_test x11_integration_test.cpp x11_client.cpp)
    target_link_libraries(
        upscale_x11_integration_test
        PRIVATE Qt6::Test Qt6::DBus KF6::ConfigCore XCB::XCB XCB::RANDR
    )
    add_test(
        NAME upscale-x11-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_x11_integration_test> --x11
    )
    # Eight fullscreen transitions, each waiting on a client that commits a
    # buffer every three seconds on the slowest machine this runs on, need
    # more headroom than one session of the other test does.
    set_tests_properties(upscale-x11-integration PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    if(NOT UPSCALE_SESSION_TESTS)
        # Disabled rather than unregistered, so the test binaries are still
        # compiled and linked by a package build and only the sessions are
        # skipped. ctest reports them as disabled, which is visible in the log
        # rather than silently absent from it.
        set_tests_properties(upscale-integration upscale-x11-integration PROPERTIES DISABLED TRUE)
    endif()
    if(UPSCALE_SANITIZER)
        set_tests_properties(
            upscale-integration
            upscale-x11-integration
            PROPERTIES
                TIMEOUT 1200
                ENVIRONMENT
                    "UPSCALE_SANITIZER_RUNTIME=${UPSCALE_SANITIZER_RUNTIME};LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=1"
        )
    endif()
endif()
add_executable(
    upscale_render_test
    render_test.cpp
    ../src/plugins/upscale/overlay.cpp
    ../src/plugins/upscale/scaler.cpp
    ../src/plugins/upscale/upscale.qrc
)
target_include_directories(upscale_render_test PRIVATE ../src/plugins/upscale)
target_link_libraries(upscale_render_test PRIVATE kwin Qt6::Test)
add_test(NAME upscale-render COMMAND upscale_render_test)
set_tests_properties(
    upscale-render
    PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;QT_QPA_PLATFORM=offscreen"
)
if(KWin_VERSION VERSION_LESS 6.7)
    add_test(NAME upscale-render-gles COMMAND upscale_render_test)
    set_tests_properties(
        upscale-render-gles
        PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;KWIN_COMPOSE=O2ES;QT_QPA_PLATFORM=offscreen"
    )
endif()

add_executable(
    upscale_framestatistics_test
    framestatistics_test.cpp
    ../src/plugins/upscale/framestatistics.cpp
)
target_include_directories(upscale_framestatistics_test PRIVATE ../src/plugins/upscale)
target_link_libraries(upscale_framestatistics_test PRIVATE Qt6::Test)
add_test(NAME upscale-framestatistics COMMAND upscale_framestatistics_test)

add_executable(
    upscale_render_shape_test
    render_shape_test.cpp
    ../src/plugins/upscale/scaler.cpp
    ../src/plugins/upscale/upscale.qrc
)
target_include_directories(upscale_render_shape_test PRIVATE ../src/plugins/upscale)
target_link_libraries(upscale_render_shape_test PRIVATE kwin Qt6::Test)
add_test(NAME upscale-render-shape COMMAND upscale_render_shape_test)
set_tests_properties(
    upscale-render-shape
    PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;QT_QPA_PLATFORM=offscreen"
)
if(KWin_VERSION VERSION_LESS 6.7)
    add_test(NAME upscale-render-shape-gles COMMAND upscale_render_shape_test)
    set_tests_properties(
        upscale-render-shape-gles
        PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;KWIN_COMPOSE=O2ES;QT_QPA_PLATFORM=offscreen"
    )
endif()

add_executable(upscale_application_test application_test.cpp ../src/plugins/upscale/application.cpp)
target_include_directories(upscale_application_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(
    upscale_application_test
    PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale"
)
target_link_libraries(upscale_application_test PRIVATE kwin Qt6::Test KF6::I18n KF6::ConfigCore)
target_compile_definitions(
    upscale_application_test
    PRIVATE
        -DUPSCALE_APPLICATION_DEFAULTS="${CMAKE_CURRENT_SOURCE_DIR}/../src/plugins/upscale/kwinupscalerc"
)
add_test(NAME upscale-application COMMAND upscale_application_test)

set(snapshot_test_sources
    snapshot_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/refusaltext.cpp
)
kconfig_add_kcfg_files(snapshot_test_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(upscale_snapshot_test ${snapshot_test_sources})
target_include_directories(upscale_snapshot_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(upscale_snapshot_test PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale")
target_link_libraries(
    upscale_snapshot_test
    PRIVATE kwin Qt6::Test KF6::ConfigGui KF6::I18n Libdrm::Libdrm
)
add_test(NAME upscale-snapshot COMMAND upscale_snapshot_test)

set(headsup_test_sources
    headsup_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/refusaltext.cpp
)
kconfig_add_kcfg_files(headsup_test_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(upscale_headsup_test ${headsup_test_sources})
target_include_directories(upscale_headsup_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(upscale_headsup_test PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale")
target_link_libraries(
    upscale_headsup_test
    PRIVATE kwin Qt6::Test KF6::ConfigGui KF6::I18n Libdrm::Libdrm
)
add_test(NAME upscale-headsup COMMAND upscale_headsup_test)

set(display_test_sources
    display_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/display.cpp
    ../src/plugins/upscale/framestatistics.cpp
    ../src/plugins/upscale/overlay.cpp
    ../src/plugins/upscale/placement.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/refusaltext.cpp
)
kconfig_add_kcfg_files(display_test_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(upscale_display_test ${display_test_sources})
target_include_directories(upscale_display_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(upscale_display_test PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale")
target_link_libraries(
    upscale_display_test
    PRIVATE kwin Qt6::Test KF6::ConfigGui KF6::I18n Libdrm::Libdrm
)
add_test(NAME upscale-display COMMAND upscale_display_test)
set_tests_properties(
    upscale-display
    PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;QT_QPA_PLATFORM=offscreen"
)

if(KWin_VERSION VERSION_LESS 6.7)
    add_test(NAME upscale-display-gles COMMAND upscale_display_test)
    set_tests_properties(
        upscale-display-gles
        PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;KWIN_COMPOSE=O2ES;QT_QPA_PLATFORM=offscreen"
    )
endif()

if(KWIN_BUILD_KCMS)
    set(settings_module_SOURCES
        ../src/plugins/upscale/application.cpp
        ../src/plugins/upscale/applicationeditor.cpp
        ../src/plugins/upscale/placement.cpp
        ../src/plugins/upscale/supportinformation.cpp
        ../src/plugins/upscale/upscale_config.cpp
        ../src/plugins/upscale/resolutionchoice.cpp
    )
    kconfig_add_kcfg_files(settings_module_SOURCES ../src/plugins/upscale/upscaleconfig.kcfgc)
    # Exercise both the upstream form without project identity and the packaged
    # settings module, which compares the full identity reported by KWin. The
    # application editor is its own page and is tested once.
    foreach(variant IN ITEMS config config-buildinfo application-editor)
        set(test_target "upscale_${variant}_test")
        if(variant STREQUAL "application-editor")
            set(test_source application_editor_test.cpp)
        else()
            set(test_source config_test.cpp)
        endif()
        add_executable(${test_target} ${test_source} ${settings_module_SOURCES})
        target_include_directories(${test_target} PRIVATE ../src/plugins/upscale)
        target_link_libraries(
            ${test_target}
            PRIVATE KF6::KCMUtils KF6::ConfigGui KF6::CoreAddons KF6::I18n Qt6::DBus Qt6::Test
        )
        # The page edits the application list this build installs, so the tests
        # read that file rather than a fixture standing in for it.
        target_compile_definitions(
            ${test_target}
            PRIVATE
                -DUPSCALE_APPLICATION_DEFAULTS="${CMAKE_CURRENT_SOURCE_DIR}/../src/plugins/upscale/kwinupscalerc"
                -DTRANSLATION_DOMAIN="kwin_effect_upscale"
        )
        add_test(NAME upscale-${variant} COMMAND ${test_target})
        set_tests_properties(upscale-${variant} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
    upscale_add_build_info(upscale_config-buildinfo_test)
endif()
