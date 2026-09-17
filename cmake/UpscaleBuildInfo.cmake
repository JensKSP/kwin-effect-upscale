# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compiles which commit, branch and date a binary came from into the plugin,
# without a header that would make every source depend on the current commit.
#
# What a commit costs to rebuild: one translation unit and a link. The generator
# rewrites the source only when its content actually changed, so building twice
# on the same commit recompiles nothing at all.

set(UPSCALE_BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/buildinfo")
set(UPSCALE_BUILD_INFO_SOURCE "${UPSCALE_BUILD_INFO_DIR}/buildinfo.cpp")
set(UPSCALE_BUILD_INFO_TEMPLATE "${CMAKE_SOURCE_DIR}/src/buildinfo/buildinfo.cpp.in")
set(UPSCALE_BUILD_INFO_GENERATOR "${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake")
set(UPSCALE_PACKAGE_VERSION "" CACHE STRING "Version supplied by the package changelog")

set(UPSCALE_BUILD_INFO_COMMAND
    ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTEMPLATE=${UPSCALE_BUILD_INFO_TEMPLATE}
    -DOUTPUT=${UPSCALE_BUILD_INFO_SOURCE}
    -DPROJECT_VERSION=${PROJECT_VERSION}
    -DPACKAGE_VERSION=${UPSCALE_PACKAGE_VERSION}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -P
    ${UPSCALE_BUILD_INFO_GENERATOR}
)

# Once now, so that the file exists while the build system is being written, and
# once per build below, so that it is right rather than merely present.
file(MAKE_DIRECTORY "${UPSCALE_BUILD_INFO_DIR}")
execute_process(COMMAND ${UPSCALE_BUILD_INFO_COMMAND} COMMAND_ERROR_IS_FATAL ANY)

add_custom_target(
    upscale_buildinfo
    ALL
    COMMAND ${UPSCALE_BUILD_INFO_COMMAND}
    BYPRODUCTS ${UPSCALE_BUILD_INFO_SOURCE}
    COMMENT "Checking which commit this build is"
    VERBATIM
)

# Adds the build information to a target. Called from the shim so that
# src/plugins/upscale/CMakeLists.txt stays what KWin would carry.
function(upscale_add_build_info target)
    set_source_files_properties(${UPSCALE_BUILD_INFO_SOURCE} PROPERTIES GENERATED TRUE)
    target_sources(${target} PRIVATE ${UPSCALE_BUILD_INFO_SOURCE})
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src/buildinfo)
    add_dependencies(${target} upscale_buildinfo)
endfunction()
