# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

enable_language(C)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(protocol_sources)
# The fractional scale is how Auto asks a Wayland window for a smaller buffer,
# so the test client binds it to see what it was asked, and xdg_output is
# where it reads the logical size that goes with the mode it was told.
foreach(
    path
    IN
    ITEMS
        stable/xdg-shell/xdg-shell
        stable/viewporter/viewporter
        staging/fractional-scale/fractional-scale-v1
        unstable/xdg-output/xdg-output-unstable-v1
        unstable/pointer-constraints/pointer-constraints-unstable-v1
        unstable/relative-pointer/relative-pointer-unstable-v1
)
    get_filename_component(protocol ${path} NAME)
    set(protocol_xml "${WAYLAND_PROTOCOLS}/${path}.xml")
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
add_executable(
    upscale_integration_test
    integration_test.cpp
    integration_test.h
    integration_advertisement_test.cpp
    integration_output_test.cpp
    integration_crash_test.cpp
    integration_logging_test.cpp
    integration_picture_test.cpp
    crash_game.h
    wayland_client.cpp
    wayland_client_pointer.cpp
    wayland_client_popup.cpp
    ${protocol_sources}
)
target_include_directories(upscale_integration_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
    upscale_integration_test
    PRIVATE Qt6::Test Qt6::Gui Qt6::DBus KF6::ConfigCore PkgConfig::WAYLAND_CLIENT
)
if(KWin_VERSION VERSION_LESS 6.7)
    find_package(XCB REQUIRED COMPONENTS SHAPE)
    set(driver_sources
        effect_driver.cpp
        ../src/plugins/upscale/application.cpp
        ../src/plugins/upscale/matching.cpp
        ../src/plugins/upscale/pattern.cpp
        ../src/plugins/upscale/settings.cpp
        ../src/plugins/upscale/legacysettings.cpp
        ../src/plugins/upscale/display.cpp
        ../src/plugins/upscale/diagnostics.cpp
        ../src/plugins/upscale/framestatistics.cpp
        ../src/plugins/upscale/display.h
        ../src/plugins/upscale/eligibility.cpp
        ../src/plugins/upscale/eligibility_over.cpp
        ../src/plugins/upscale/windowidentity.cpp
        ../src/plugins/upscale/x11proxypolicy.cpp
        ../src/plugins/upscale/refusaltext.cpp
        ../src/plugins/upscale/logicalsize.cpp
        ../src/plugins/upscale/modeoverride.cpp
        ../src/plugins/upscale/waylandscale.cpp
        ../src/plugins/upscale/waylandresize.cpp
        ../src/plugins/upscale/waylandinitial.cpp
        ../src/plugins/upscale/observation.cpp
        ../src/plugins/upscale/overlay.cpp
        ../src/plugins/upscale/placement.cpp
        ../src/plugins/upscale/snapshot.cpp
        ../src/plugins/upscale/snapshot_headsup.cpp
        ../src/plugins/upscale/snapshot_metrics.cpp
        ../src/plugins/upscale/upscale.cpp
        ../src/plugins/upscale/upscale_over.cpp
        ../src/plugins/upscale/upscale_display.cpp
        ../src/plugins/upscale/autorequest.cpp
        ../src/plugins/upscale/x11geometry.cpp
        ../src/plugins/upscale/x11modes.cpp
        ../src/plugins/upscale/pictureinput.cpp
        ../src/plugins/upscale/touchinput.cpp
        ../src/plugins/upscale/x11input.cpp
        ../src/plugins/upscale/x11resolution.cpp
        ../src/plugins/upscale/x11resolution_events.cpp
        ../src/plugins/upscale/x11resolution_startup.cpp
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
        PRIVATE kwin Qt6::DBus KF6::ConfigGui KF6::I18n Libdrm::Libdrm XCB::XCB XCB::RANDR XCB::RES
    )
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
    # The same client in a session with an output to take away and a scale to
    # step down from: two outputs at scale 2. Only the cases about outputs,
    # scale and a program leaving run here; the rest would buy nothing twice.
    add_test(
        NAME upscale-integration-outputs
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_integration_test> --scale=2 --outputs=2
            anOutputThatGoesAwayWhileAdvertised aProgramThatExitsBeforeRestoration
            anOutputVersionWithoutScaleIsLeftAlone aSurfaceScaleFollowsTheOutputScale
            drawsAWindowOfTheToldPixelsOverItsOutput
    )
    set_tests_properties(upscale-integration-outputs PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    # A game's X11 window in a process of its own; see x11_game_standin.cpp.
    add_executable(upscale_test_x11_game x11_game_standin.cpp x11_client.cpp x11_client_pointer.cpp)
    target_link_libraries(upscale_test_x11_game PRIVATE Qt6::Core XCB::XCB XCB::RANDR XCB::SHAPE)
    # The X11 session: the request path, and what a presented window takes in.
    add_executable(
        upscale_x11_integration_test
        x11_integration_test.cpp
        x11_integration_test.h
        x11_client.cpp
        x11_client_pointer.cpp
        x11_startup_test.cpp
        x11_input_test.cpp
        x11_crash_test.cpp
        x11_fullscreen_test.cpp
        x11_wine_test.cpp
        x11_report_test.cpp
        x11_picture_test.cpp
        crash_game.h
        x11_standin_game.h
    )
    # The stand-in game of anUnnamedProgramIsHeldAtItsFirstMapping, which the
    # Wine cases start under a Wine loader's name; see x11_game_standin.cpp.
    target_compile_definitions(
        upscale_x11_integration_test
        PRIVATE
            UPSCALE_APPLICATION_DEFAULTS="${CMAKE_SOURCE_DIR}/src/plugins/upscale/kwinupscalerc"
            UPSCALE_TEST_KWIN_VERSION="${KWin_VERSION}"
            UPSCALE_TEST_X11_GAME="$<TARGET_FILE:upscale_test_x11_game>"
    )
    add_dependencies(upscale_x11_integration_test upscale_test_x11_game)
    target_link_libraries(
        upscale_x11_integration_test
        PRIVATE Qt6::Test Qt6::DBus KF6::ConfigCore XCB::XCB XCB::RANDR XCB::SHAPE
    )
    add_test(
        NAME upscale-x11-integration
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_x11_integration_test> --x11
    )
    # Eight fullscreen transitions, each waiting on a client that commits a
    # buffer every three seconds on the slowest machine this runs on, need more
    # headroom than one session of the other test does.
    set_tests_properties(upscale-x11-integration PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    # The same session as a desktop actually has one: a single 4K screen at the
    # scale such a screen is run at. Only the tests that cross between logical
    # and device pixels run here, and the one whose connection is answered
    # before its window exists, which names a screen rather than an
    # arrangement of them. A whole second suite would buy nothing and cost a
    # second session's worth of round trips.
    add_test(
        NAME upscale-x11-scaled
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
            $<TARGET_FILE:upscale_x11_integration_test> --x11 --scale=3 --outputs=1
            coversPointerWithoutEmulatedMode aConfinedPointerReachesTheWholeWindow
            refreshesStartupInputShape coversTheScreenItWasGiven winePrefixEligibility
            answersUnlistedProgramsUnderAllApplications presentsAProcessShownItsGamesScreen
    )
    set_tests_properties(upscale-x11-scaled PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
    set(sessions
        upscale-integration
        upscale-integration-outputs
        upscale-x11-integration
        upscale-x11-scaled
    )
    # A real game that crashes where a case says, for the cases about a game
    # that disappears without warning: glmark2 as tools/prepare-crash-game.py
    # prepares it. Only where that build exists; point UPSCALE_CRASH_GAME at
    # the directory it prepared. The X11 session has one output, so that the
    # root window glmark2 sizes itself by is the screen it fills.
    if(UPSCALE_CRASH_GAME)
        add_test(
            NAME upscale-integration-crash
            COMMAND
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
                $<TARGET_FILE:upscale_integration_test> aGameThatCrashesIsLetGo
                crashingGamesLeaveNothingBehind
        )
        add_test(
            NAME upscale-x11-crash
            COMMAND
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run-integration-test.py
                $<TARGET_FILE:upscale_x11_integration_test> --x11 --outputs=1
                aGameThatCrashesWhileResizedIsLetGo
        )
        set(crash_sessions upscale-integration-crash upscale-x11-crash)
        set_tests_properties(
            ${crash_sessions}
            PROPERTIES
                TIMEOUT 900
                RUN_SERIAL TRUE
                ENVIRONMENT_MODIFICATION "UPSCALE_TEST_CRASH_GAME=set:${UPSCALE_CRASH_GAME}"
        )
        list(APPEND sessions ${crash_sessions})
    endif()
    if(NOT UPSCALE_SESSION_TESTS)
        # Disabled rather than unregistered, so the test binaries are still
        # compiled and linked by a package build and only the sessions are
        # skipped. ctest reports them as disabled, which is visible in the log
        # rather than silently absent from it.
        set_tests_properties(${sessions} PROPERTIES DISABLED TRUE)
    endif()
    if(UPSCALE_SANITIZER)
        set_tests_properties(
            ${sessions}
            PROPERTIES
                TIMEOUT 1200
                ENVIRONMENT
                    "UPSCALE_SANITIZER_RUNTIME=${UPSCALE_SANITIZER_RUNTIME};LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp:print_suppressions=1"
        )
    endif()
endif()
