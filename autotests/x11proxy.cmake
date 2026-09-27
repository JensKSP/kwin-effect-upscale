# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(upscale_x11proxy_policy_test x11proxy_policy_test.cpp)
target_link_libraries(upscale_x11proxy_policy_test PRIVATE upscale_x11_transport)
kde_target_enable_exceptions(upscale_x11proxy_policy_test PRIVATE)
add_test(NAME upscale-x11proxy-policy COMMAND upscale_x11proxy_policy_test)
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
