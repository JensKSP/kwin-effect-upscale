# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

set(UPSCALE_SANITIZER "" CACHE STRING "Sanitizer: address,undefined or thread")
set_property(CACHE UPSCALE_SANITIZER PROPERTY STRINGS "" "address,undefined" "thread")
option(UPSCALE_COVERAGE "Instrument plugin and tests for GCC coverage" OFF)
option(UPSCALE_FUZZING "Build the Clang libFuzzer resolution target" OFF)

if(UPSCALE_SANITIZER AND NOT UPSCALE_SANITIZER MATCHES "^(address,undefined|thread)$")
    message(FATAL_ERROR "UPSCALE_SANITIZER must be address,undefined or thread")
endif()
if(UPSCALE_COVERAGE AND (UPSCALE_SANITIZER OR UPSCALE_FUZZING))
    message(FATAL_ERROR "Coverage needs a separate build from sanitizers and fuzzing")
endif()
if(UPSCALE_FUZZING AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Fuzzing requires Clang")
endif()
if(UPSCALE_FUZZING AND NOT UPSCALE_SANITIZER STREQUAL "address,undefined")
    message(FATAL_ERROR "Fuzzing requires UPSCALE_SANITIZER=address,undefined")
endif()
if(UPSCALE_SANITIZER)
    if(UPSCALE_SANITIZER STREQUAL "thread")
        set(sanitizer_kind tsan)
    else()
        set(sanitizer_kind asan)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(sanitizer_library "libclang_rt.${sanitizer_kind}-${CMAKE_SYSTEM_PROCESSOR}.so")
        add_link_options(-shared-libsan)
    else()
        set(sanitizer_library "lib${sanitizer_kind}.so")
    endif()
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=${sanitizer_library}
        OUTPUT_VARIABLE UPSCALE_SANITIZER_RUNTIME
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(NOT EXISTS "${UPSCALE_SANITIZER_RUNTIME}")
        message(FATAL_ERROR "Install the compiler's shared sanitizer runtime: ${sanitizer_library}")
    endif()
    get_filename_component(sanitizer_directory "${UPSCALE_SANITIZER_RUNTIME}" DIRECTORY)
    add_link_options("-Wl,-rpath,${sanitizer_directory}")
    add_compile_options(
        -fsanitize=${UPSCALE_SANITIZER}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )
    add_link_options(-fsanitize=${UPSCALE_SANITIZER})
    # KDE normally rejects undefined symbols in modules. Clang's sanitizer
    # runtime is supplied by the executable (or preloaded for stock KWin).
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")
    string(REPLACE "-Wl,--no-undefined" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
endif()
if(UPSCALE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "Coverage requires GCC and the matching gcov")
    endif()
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()
