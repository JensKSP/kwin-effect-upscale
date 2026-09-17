# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs at build time, not at configure time, so that the commit a binary claims
# to be is the commit it was built from and not the one that happened to be
# checked out when cmake last ran.
#
# It writes one translation unit and writes it only when its content changed, so
# a rebuild after a commit recompiles that single file and relinks. Nothing here
# reaches a header; a header would drag every including source through the
# compiler again for a hash that says nothing about their content.
#
# Invoked as:
#   cmake -DSOURCE_DIR=... -DTEMPLATE=... -DOUTPUT=... -DPROJECT_VERSION=...
#         -P cmake/GenerateBuildInfo.cmake

cmake_minimum_required(VERSION 3.24)

function(run_git output_variable)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE result
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE status
    )
    if(NOT status EQUAL 0)
        set(result "")
    endif()
    set(${output_variable} "${result}" PARENT_SCOPE)
endfunction()

# A release tarball has no .git, and that is a normal way to build this. The
# version then is the one CMake was told, with no git detail claimed.
find_package(Git QUIET)
set(UPSCALE_GIT_HASH "")
set(UPSCALE_GIT_BRANCH "")
set(UPSCALE_GIT_DIRTY "")
set(UPSCALE_GIT_DATE "")

if(GIT_EXECUTABLE AND EXISTS "${SOURCE_DIR}/.git")
    run_git(UPSCALE_GIT_HASH rev-parse --short=10 HEAD)
    run_git(UPSCALE_GIT_DATE show -s --format=%cs HEAD)
    run_git(git_status status --porcelain --untracked-files=no)
    if(NOT git_status STREQUAL "")
        set(UPSCALE_GIT_DIRTY "-dirty")
    endif()

    # CI checks out a detached HEAD, where git knows no branch name. The forge
    # does, and says so in the environment.
    if(DEFINED ENV{GITHUB_HEAD_REF} AND NOT "$ENV{GITHUB_HEAD_REF}" STREQUAL "")
        set(UPSCALE_GIT_BRANCH "$ENV{GITHUB_HEAD_REF}")
    elseif(DEFINED ENV{GITHUB_REF_NAME} AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
        set(UPSCALE_GIT_BRANCH "$ENV{GITHUB_REF_NAME}")
    elseif(DEFINED ENV{CI_COMMIT_REF_NAME} AND NOT "$ENV{CI_COMMIT_REF_NAME}" STREQUAL "")
        set(UPSCALE_GIT_BRANCH "$ENV{CI_COMMIT_REF_NAME}")
    else()
        run_git(UPSCALE_GIT_BRANCH rev-parse --abbrev-ref HEAD)
        if(UPSCALE_GIT_BRANCH STREQUAL "HEAD")
            set(UPSCALE_GIT_BRANCH "")
        endif()
    endif()
endif()

# The version a build calls itself. A build sitting exactly on a release is that
# release and says so plainly; anything else carries the commit date and hash,
# ordered the way Debian orders a snapshot after a release.
set(UPSCALE_VERSION "${PROJECT_VERSION}")
if(NOT UPSCALE_GIT_HASH STREQUAL "")
    run_git(exact_tag tag --points-at HEAD --list "v${PROJECT_VERSION}")
    if(NOT exact_tag STREQUAL "v${PROJECT_VERSION}" OR NOT UPSCALE_GIT_DIRTY STREQUAL "")
        string(REPLACE "-" "" compact_date "${UPSCALE_GIT_DATE}")
        set(UPSCALE_VERSION
            "${PROJECT_VERSION}+git${compact_date}.${UPSCALE_GIT_HASH}${UPSCALE_GIT_DIRTY}"
        )
    endif()
elseif(EXISTS "${SOURCE_DIR}/source-version")
    file(STRINGS "${SOURCE_DIR}/source-version" archive_version REGEX "^[0-9]")
    if(NOT archive_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(\\+git[0-9]+\\.[0-9a-f]+)?$")
        message(FATAL_ERROR "Invalid version in source-version: ${archive_version}")
    endif()
    set(UPSCALE_VERSION "${archive_version}")
endif()

# debian/rules passes the changelog version explicitly. Packaging changes the
# changelog in CI; those changes must not turn an official package into a dirty
# development snapshot.
if(DEFINED PACKAGE_VERSION AND NOT PACKAGE_VERSION STREQUAL "")
    set(UPSCALE_VERSION "${PACKAGE_VERSION}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/BuildInfoDate.cmake")

# Git permits quotes in branch names. Preserve them as data in C++ literals.
foreach(variable UPSCALE_VERSION UPSCALE_GIT_BRANCH)
    string(REPLACE "\\" "\\\\" ${variable} "${${variable}}")
    string(REPLACE "\"" "\\\"" ${variable} "${${variable}}")
    string(REPLACE "\n" "\\n" ${variable} "${${variable}}")
    string(REPLACE "\r" "\\r" ${variable} "${${variable}}")
endforeach()

configure_file("${TEMPLATE}" "${OUTPUT}.candidate" @ONLY)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.candidate" "${OUTPUT}"
    COMMAND_ERROR_IS_FATAL ANY
)
file(REMOVE "${OUTPUT}.candidate")
