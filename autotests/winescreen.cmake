# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# The companion that describes a screen in a Wine prefix lives outside the
# plugin folder and needs nothing but Qt Core.
foreach(
    test
    IN
    ITEMS wine_registry wine_display_mode wine_prefix wine_screen_write wine_screen_helper
)
    add_executable(upscale_${test}_test ${test}_test.cpp)
    target_link_libraries(upscale_${test}_test PRIVATE upscale_winescreen Qt6::Test)
    string(REPLACE "_" "-" name ${test})
    add_test(NAME upscale-${name} COMMAND upscale_${test}_test)
endforeach()

target_sources(
    upscale_wine_screen_helper_test
    PRIVATE wine_screen_helper_fixture.cpp wine_screen_helper_test.h
)
