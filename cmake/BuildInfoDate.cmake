# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# Keep the date of an unchanged build. Include the source contents, not just
# Git's dirty flag: a second edit in an already dirty tree is still a new build.
file(GLOB_RECURSE build_inputs LIST_DIRECTORIES false "${SOURCE_DIR}/src/*" "${SOURCE_DIR}/cmake/*")
list(APPEND build_inputs "${SOURCE_DIR}/CMakeLists.txt" "${TEMPLATE}")
if(DEFINED BUILD_DIR AND EXISTS "${BUILD_DIR}/CMakeCache.txt")
    list(APPEND build_inputs "${BUILD_DIR}/CMakeCache.txt")
endif()
set(identity "${UPSCALE_VERSION}|${UPSCALE_GIT_HASH}|${UPSCALE_GIT_BRANCH}")
foreach(input IN LISTS build_inputs)
    file(SHA256 "${input}" digest)
    string(APPEND identity "|${input}:${digest}")
endforeach()
string(SHA256 fingerprint "${identity}")

set(previous_state "")
if(EXISTS "${OUTPUT}.state")
    file(READ "${OUTPUT}.state" previous_state)
endif()

# Packagers supply SOURCE_DATE_EPOCH; CMake honours it directly. Otherwise the
# clock is read only for a new build identity, so a no-op build remains a no-op.
if(NOT DEFINED ENV{SOURCE_DATE_EPOCH} AND previous_state MATCHES "^${fingerprint}\n([^\n]+)\n$")
    set(UPSCALE_BUILD_DATE "${CMAKE_MATCH_1}")
else()
    string(TIMESTAMP UPSCALE_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
    file(WRITE "${OUTPUT}.state" "${fingerprint}\n${UPSCALE_BUILD_DATE}\n")
endif()
