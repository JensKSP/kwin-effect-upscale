# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# The companion that gives a Wine or Proton game a virtual desktop of the size
# the effect chose. It is not part of the plugin, and nothing in
# src/plugins/upscale depends on it.
add_library(
    upscale_winedesktop
    STATIC
    winedesktopwrite.cpp
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
target_link_libraries(upscale_winedesktop PUBLIC Qt6::Core)
