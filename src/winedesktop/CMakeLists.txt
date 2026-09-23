# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# The helper that tells a Wine or Proton game's prefix its screen is the size the
# effect chose. It is not part of the plugin: the plugin only knows the
# optional org.kde.KWin.Upscale.Helper1 interface it defines, and works the same
# without anything answering it.
add_library(
    upscale_winedesktop
    STATIC
    winedesktophelper.cpp
    winedesktoprecord.cpp
    winedesktopwrite.cpp
    winedisplaymode.cpp
    wineprefix.cpp
    wineregistry.cpp
    wineserverlock.cpp
)
# Only Linux lets a process read another one's environment, through /proc.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(upscale_winedesktop PRIVATE wineprocess_proc.cpp)
else()
    target_sources(upscale_winedesktop PRIVATE wineprocess_unsupported.cpp)
endif()
target_include_directories(upscale_winedesktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(upscale_winedesktop PUBLIC -DTRANSLATION_DOMAIN="kwin_upscale_helper")
target_link_libraries(upscale_winedesktop PUBLIC Qt6::Core KF6::I18n)

# Steam and Proton run on Linux only, so the service is built there alone.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(kwin-upscale-helper main.cpp winedesktopservice.cpp)
    target_link_libraries(kwin-upscale-helper PRIVATE upscale_winedesktop Qt6::DBus)
    install(TARGETS kwin-upscale-helper DESTINATION ${KDE_INSTALL_LIBEXECDIR})
    configure_file(
        org.kde.KWin.Upscale.Helper.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KWin.Upscale.Helper.service
        @ONLY
    )
    configure_file(
        plasma-kwin-upscale-helper.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/plasma-kwin-upscale-helper.service
        @ONLY
    )
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kde.KWin.Upscale.Helper.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
    )
    install(
        FILES ${CMAKE_CURRENT_BINARY_DIR}/plasma-kwin-upscale-helper.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
    )
endif()
