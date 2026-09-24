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

include(sessions.cmake)

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

# settings.cpp reads the global layer through the generated configuration, so
# this test needs the kcfg generated for it as the others do.
kconfig_add_kcfg_files(application_test_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(
    upscale_application_test
    application_test.cpp
    application_export_test.cpp
    legacysettings_test.cpp
    matching_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/matching.cpp
    ../src/plugins/upscale/pattern.cpp
    ../src/plugins/upscale/settings.cpp
    ../src/plugins/upscale/legacysettings.cpp
    ${application_test_sources}
)
target_include_directories(upscale_application_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(
    upscale_application_test
    PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale"
)
target_link_libraries(upscale_application_test PRIVATE kwin Qt6::Test KF6::I18n KF6::ConfigGui)
target_compile_definitions(
    upscale_application_test
    PRIVATE
        -DUPSCALE_APPLICATION_DEFAULTS="${CMAKE_CURRENT_SOURCE_DIR}/../src/plugins/upscale/kwinupscalerc"
)
add_test(NAME upscale-application COMMAND upscale_application_test)

set(snapshot_test_sources
    snapshot_test.cpp
    snapshot_request_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/matching.cpp
    ../src/plugins/upscale/pattern.cpp
    ../src/plugins/upscale/settings.cpp
    ../src/plugins/upscale/legacysettings.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/windowidentity.cpp
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
    ../src/plugins/upscale/matching.cpp
    ../src/plugins/upscale/pattern.cpp
    ../src/plugins/upscale/settings.cpp
    ../src/plugins/upscale/legacysettings.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/windowidentity.cpp
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

set(placement_test_sources
    placement_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/matching.cpp
    ../src/plugins/upscale/pattern.cpp
    ../src/plugins/upscale/settings.cpp
    ../src/plugins/upscale/legacysettings.cpp
    ../src/plugins/upscale/overlay.cpp
    ../src/plugins/upscale/placement.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/windowidentity.cpp
    ../src/plugins/upscale/refusaltext.cpp
)
kconfig_add_kcfg_files(placement_test_sources ../src/plugins/upscale/upscaleconfig.kcfgc)
add_executable(upscale_placement_test ${placement_test_sources})
target_include_directories(upscale_placement_test PRIVATE ../src/plugins/upscale)
target_compile_definitions(
    upscale_placement_test
    PRIVATE -DTRANSLATION_DOMAIN="kwin_effect_upscale"
)
target_link_libraries(
    upscale_placement_test
    PRIVATE kwin Qt6::Test KF6::ConfigGui KF6::I18n Libdrm::Libdrm
)
# No OpenGL context and no rasteriser: this one asserts geometry, so it needs
# only a platform that can measure a font.
add_test(NAME upscale-placement COMMAND upscale_placement_test)
set_tests_properties(upscale-placement PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

set(display_test_sources
    display_test.cpp
    display_test.h
    display_settings_test.cpp
    ../src/plugins/upscale/application.cpp
    ../src/plugins/upscale/matching.cpp
    ../src/plugins/upscale/pattern.cpp
    ../src/plugins/upscale/settings.cpp
    ../src/plugins/upscale/legacysettings.cpp
    ../src/plugins/upscale/display.cpp
    ../src/plugins/upscale/framestatistics.cpp
    ../src/plugins/upscale/overlay.cpp
    ../src/plugins/upscale/placement.cpp
    ../src/plugins/upscale/snapshot.cpp
    ../src/plugins/upscale/snapshot_metrics.cpp
    ../src/plugins/upscale/eligibility.cpp
    ../src/plugins/upscale/windowidentity.cpp
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
        ../src/plugins/upscale/matching.cpp
        ../src/plugins/upscale/pattern.cpp
        ../src/plugins/upscale/settings.cpp
        ../src/plugins/upscale/legacysettings.cpp
        ../src/plugins/upscale/applicationeditor.cpp
        ../src/plugins/upscale/applicationeditor_details.cpp
        ../src/plugins/upscale/applicationeditor_window.cpp
        ../src/plugins/upscale/settingcontrols.cpp
        ../src/plugins/upscale/inheritedmark.cpp
        ../src/plugins/upscale/identitycontrols.cpp
        ../src/plugins/upscale/methodcontrols.cpp
        ../src/plugins/upscale/placement.cpp
        ../src/plugins/upscale/upscale_config.cpp
        ../src/plugins/upscale/preparedlist.cpp
        ../src/plugins/upscale/displaycontrols.cpp
        ../src/plugins/upscale/applicationcontrols.cpp
        ../src/plugins/upscale/resolutionchoice.cpp
        ../src/plugins/upscale/resolutionpreview.cpp
        ../src/plugins/upscale/sliderfield.cpp
    )
    kconfig_add_kcfg_files(settings_module_SOURCES ../src/plugins/upscale/upscaleconfig.kcfgc)
    # Exercise both the upstream form without project identity and the packaged
    # settings module, which compares the full identity reported by KWin. The
    # application editor is its own page and is tested once.
    foreach(
        variant
        IN
        ITEMS
            config
            config-buildinfo
            application-editor
            identity-controls
            application-list
            prepared-list
    )
        set(test_target "upscale_${variant}_test")
        if(variant STREQUAL "application-editor")
            # The stand-ins for KWin and the effect are QObjects, so moc reads
            # their header too.
            set(test_source application_editor_test.cpp editor_stand_ins.h)
        elseif(variant STREQUAL "identity-controls")
            set(test_source identity_controls_test.cpp editor_stand_ins.h)
        elseif(variant STREQUAL "application-list")
            set(test_source application_list_test.cpp)
        elseif(variant STREQUAL "prepared-list")
            set(test_source prepared_list_test.cpp)
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

include(winescreen.cmake)
