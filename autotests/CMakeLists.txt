# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
add_executable(
    upscale_render_test
    render_test.cpp
    ../src/plugins/upscale/scaler.cpp
    ../src/plugins/upscale/upscale.qrc
)
target_include_directories(upscale_render_test PRIVATE ../src/plugins/upscale)
target_link_libraries(upscale_render_test PRIVATE kwin Qt6::Test)
add_test(NAME upscale-render COMMAND upscale_render_test)
set_tests_properties(upscale-render PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1")
if(KWin_VERSION VERSION_LESS 6.7)
    add_test(NAME upscale-render-gles COMMAND upscale_render_test)
    set_tests_properties(
        upscale-render-gles
        PROPERTIES ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;KWIN_COMPOSE=O2ES"
    )
endif()

if(KWIN_BUILD_KCMS)
    set(config_test_SOURCES config_test.cpp ../src/plugins/upscale/upscale_config.cpp)
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
