# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# CMake 3.24 introduced CMAKE_COMPILE_WARNING_AS_ERROR; Trixie ships 3.31.6.
cmake_minimum_required(VERSION 3.24)

project(kwin-effect-upscale VERSION 0.1.0 LANGUAGES CXX)

# One build directory beside the sources, never inside them. An in-source
# configure scatters generated files through the tree, and from there they end
# up in a commit.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds are not allowed. Use a build directory, for example:\n"
        "    cmake -B build -S .\n"
        "Then remove the CMakeCache.txt and CMakeFiles/ this attempt left behind."
    )
endif()

set(QT_MIN_VERSION 6.8.0)
set(KF_MIN_VERSION 6.13.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_KDE_MODULE_DIR} ${CMAKE_SOURCE_DIR}/cmake)

# KWin builds at level 5.82; we ask for more (-pedantic, -Wzero-as-null-pointer-constant,
# -Werror=undef) because the strictness lives in this shell, never in the plugin folder.
set(KDE_COMPILERSETTINGS_LEVEL 6.13.0)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(FeatureSummary)
include(ECMSetupVersion)
ecm_setup_version(${PROJECT_VERSION} VARIABLE_PREFIX UPSCALE)

# Same as KWin: src/CMakeLists.txt lines 7-8 of v6.3.6.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
# KWIN_EFFECT_FACTORY_* expands into a class carrying Q_OBJECT. AutoMoc cannot
# see through the macro, so the names are registered here; otherwise it warns
# that main.cpp includes main.moc without a Q_OBJECT in sight.
list(
    APPEND CMAKE_AUTOMOC_MACRO_NAMES
    KWIN_EFFECT_FACTORY
    KWIN_EFFECT_FACTORY_ENABLED
    KWIN_EFFECT_FACTORY_SUPPORTED
    KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED
)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings are errors unless a packager says otherwise:
#   cmake -DCMAKE_COMPILE_WARNING_AS_ERROR=OFF
if(NOT DEFINED CMAKE_COMPILE_WARNING_AS_ERROR)
    set(CMAKE_COMPILE_WARNING_AS_ERROR ON)
endif()

# The Qt switches KWin sets. The last two exist only in KWin master; we take them
# from the start so the plugin never grows code that would fail upstream.
add_definitions(
    -DQT_NO_KEYWORDS
    -DQT_USE_QSTRINGBUILDER
    -DQT_NO_URL_CAST_FROM_STRING
    -DQT_NO_CAST_TO_ASCII
    -DQT_NO_FOREACH
    -DQT_NO_CONTEXTLESS_CONNECT
    -DQT_NO_NARROWING_CONVERSIONS_IN_CONNECT
)

find_package(
    Qt6
    ${QT_MIN_VERSION}
    REQUIRED
    CONFIG
    COMPONENTS Core Gui Widgets DBus OpenGL
)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n)
find_package(KWin REQUIRED COMPONENTS kwineffects)

# Build the configuration page the way KWin does when its KCMs are enabled.
option(KWIN_BUILD_KCMS "Build the effect configuration page" ON)

include(KWinBuiltinEffectShim)

add_subdirectory(src/plugins/upscale)

# `make clang-format` the way every KDE project offers it. The git hook itself
# is not installed from here: ECM's kde_configure_git_pre_commit_hook writes
# .git/hooks/pre-commit, and so does `pre-commit install`, which would leave two
# systems fighting over one file. .pre-commit-config.yaml runs clang-format with
# the same .clang-format, plus the checks ECM has no hook for.
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
