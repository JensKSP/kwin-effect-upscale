# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(upscale_x11proxy_policy_test x11proxy_policy_test.cpp)
target_link_libraries(upscale_x11proxy_policy_test PRIVATE upscale_x11_transport)
kde_target_enable_exceptions(upscale_x11proxy_policy_test PRIVATE)
add_test(NAME upscale-x11proxy-policy COMMAND upscale_x11proxy_policy_test)
add_executable(upscale_x11proxy_hardening_test x11proxy_hardening_test.cpp)
target_link_libraries(upscale_x11proxy_hardening_test PRIVATE upscale_x11_transport)
kde_target_enable_exceptions(upscale_x11proxy_hardening_test PRIVATE)
add_test(NAME upscale-x11proxy-hardening COMMAND upscale_x11proxy_hardening_test)
add_executable(upscale_x11proxy_display_test x11proxy_display_test.cpp)
target_link_libraries(upscale_x11proxy_display_test PRIVATE upscale_x11_transport)
kde_target_enable_exceptions(upscale_x11proxy_display_test PRIVATE)
add_test(NAME upscale-x11proxy-display COMMAND upscale_x11proxy_display_test)
add_executable(upscale_x11proxy_transport_driver x11proxy_transport_driver.cpp)
target_link_libraries(upscale_x11proxy_transport_driver PRIVATE upscale_x11_transport)
add_test(
    NAME upscale-x11proxy-transport
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_SOURCE_DIR}/tools/test_x11proxy_transport.py
)

set_tests_properties(
    upscale-x11proxy-transport
    PROPERTIES
        ENVIRONMENT "UPSCALE_X11_TRANSPORT_DRIVER=$<TARGET_FILE:upscale_x11proxy_transport_driver>"
)

add_executable(upscale_x11proxy_identity_test x11proxy_identity_test.cpp)
target_link_libraries(upscale_x11proxy_identity_test PRIVATE upscale_x11_transport)
kde_target_enable_exceptions(upscale_x11proxy_identity_test PRIVATE)
add_test(NAME upscale-x11proxy-identity COMMAND upscale_x11proxy_identity_test)
add_executable(upscale_x11proxy_startup_test x11proxy_startup_test.cpp ../src/x11proxy/startup.cpp)
target_include_directories(upscale_x11proxy_startup_test PRIVATE ../src/x11proxy)
target_link_libraries(upscale_x11proxy_startup_test PRIVATE Qt6::Test KF6::ConfigCore)
add_test(NAME upscale-x11proxy-startup COMMAND upscale_x11proxy_startup_test)

# The whole transport under a bus of its own: the effect's side of each
# connection's question is answered by a stand-in on that bus.
add_executable(
    upscale_x11proxy_session_test
    x11proxy_session_test.cpp
    ../src/x11proxy/session.cpp
    ../src/x11proxy/connection.cpp
    ../src/x11proxy/startup.cpp
    ../src/x11proxy/lifecycle.cpp
)
target_include_directories(upscale_x11proxy_session_test PRIVATE ../src/x11proxy)
target_link_libraries(
    upscale_x11proxy_session_test
    PRIVATE upscale_x11_transport Qt6::DBus Qt6::Test KF6::ConfigCore KF6::I18n KF6::CoreAddons
)
target_compile_definitions(
    upscale_x11proxy_session_test
    PRIVATE TRANSLATION_DOMAIN="kwin_effect_upscale"
)
kde_target_enable_exceptions(upscale_x11proxy_session_test PRIVATE)
# How a session ends, and what it keeps while it runs: the same transport,
# once in a process of its own that the test sends signals to.
add_executable(
    upscale_x11proxy_shutdown_test
    x11proxy_shutdown_test.cpp
    ../src/x11proxy/session.cpp
    ../src/x11proxy/connection.cpp
    ../src/x11proxy/startup.cpp
    ../src/x11proxy/lifecycle.cpp
)
target_include_directories(upscale_x11proxy_shutdown_test PRIVATE ../src/x11proxy)
target_link_libraries(
    upscale_x11proxy_shutdown_test
    PRIVATE upscale_x11_transport Qt6::DBus Qt6::Test KF6::ConfigCore KF6::I18n KF6::CoreAddons
)
target_compile_definitions(
    upscale_x11proxy_shutdown_test
    PRIVATE TRANSLATION_DOMAIN="kwin_effect_upscale"
)
kde_target_enable_exceptions(upscale_x11proxy_shutdown_test PRIVATE)
find_program(UPSCALE_DBUS_RUN_SESSION dbus-run-session)
if(UPSCALE_DBUS_RUN_SESSION)
    add_test(
        NAME upscale-x11proxy-session
        COMMAND ${UPSCALE_DBUS_RUN_SESSION} -- $<TARGET_FILE:upscale_x11proxy_session_test>
    )
    add_test(
        NAME upscale-x11proxy-shutdown
        COMMAND ${UPSCALE_DBUS_RUN_SESSION} -- $<TARGET_FILE:upscale_x11proxy_shutdown_test>
    )
endif()

# Used by the VM-only XTS runner, not a standalone ctest case.
add_executable(
    upscale_x11proxy_conformance
    x11proxy_conformance.cpp
    ../src/x11proxy/session.cpp
    ../src/x11proxy/connection.cpp
    ../src/x11proxy/startup.cpp
    ../src/x11proxy/lifecycle.cpp
)
target_include_directories(
    upscale_x11proxy_conformance
    PRIVATE ../src/x11proxy ${CMAKE_BINARY_DIR}/src/x11proxy
)
target_link_libraries(
    upscale_x11proxy_conformance
    PRIVATE upscale_x11_transport Qt6::DBus KF6::ConfigCore KF6::I18n KF6::CoreAddons
)
target_compile_definitions(
    upscale_x11proxy_conformance
    PRIVATE TRANSLATION_DOMAIN="kwin_effect_upscale"
)
