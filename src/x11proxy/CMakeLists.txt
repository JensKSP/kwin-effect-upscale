# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
find_program(
    UPSCALE_STOCK_XWAYLAND
    NAMES Xwayland
    PATHS /usr/bin /usr/local/bin
    NO_DEFAULT_PATH
    REQUIRED
)
set(UPSCALE_X11_LAUNCHER_DIR "${KDE_INSTALL_FULL_LIBEXECDIR}/kwin-upscale-x11")
configure_file(paths.h.in paths.h @ONLY)
configure_file(session.sh.in session.sh @ONLY)
add_library(
    upscale_x11_transport
    STATIC
    relay.cpp
    policy.cpp
    framer.cpp
    policy_identity.cpp
    policy_display.cpp
    registry.cpp
    display.cpp
    display_legacy.cpp
    identity.cpp
)

# Only Linux lets a process read another one's command line and environment,
# through /proc. Elsewhere no connection is identified and none is rewritten.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(upscale_x11_transport PRIVATE identity_proc.cpp)
else()
    target_sources(upscale_x11_transport PRIVATE identity_unsupported.cpp)
endif()
target_link_libraries(upscale_x11_transport PUBLIC Qt6::Core)
kde_target_enable_exceptions(upscale_x11_transport PRIVATE)
target_include_directories(upscale_x11_transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(
    upscale_x11_proxy
    main.cpp
    session.cpp
    connection.cpp
    startup.cpp
    lifecycle.cpp
)
set_target_properties(upscale_x11_proxy PROPERTIES OUTPUT_NAME Xwayland)
target_include_directories(upscale_x11_proxy PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(
    upscale_x11_proxy
    PRIVATE upscale_x11_transport Qt6::DBus KF6::ConfigCore KF6::I18n KF6::CoreAddons
)
target_compile_definitions(upscale_x11_proxy PRIVATE TRANSLATION_DOMAIN="kwin_effect_upscale")
install(TARGETS upscale_x11_proxy DESTINATION ${UPSCALE_X11_LAUNCHER_DIR})
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/session.sh
    DESTINATION ${KDE_INSTALL_CONFDIR}/plasma-workspace/env
    RENAME kwin-upscale-x11.sh
)
