# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later

# CMake 3.24 introduced CMAKE_COMPILE_WARNING_AS_ERROR; Trixie ships 3.31.6.
cmake_minimum_required(VERSION 3.24)

project(kwin-effect-upscale VERSION 0.2.0 LANGUAGES CXX)

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
find_package(Libdrm REQUIRED)
find_package(XCB REQUIRED COMPONENTS XCB RANDR)

# Which generation of KWin's effect API this KWin has, asked of KWin rather
# than guessed from a file name.
#
# The effect's paint callbacks returned void and now return bool, and the same
# change brought RenderView::renderDevice() and a renderItem() that reports
# failure. Probing for a header instead got this wrong: core/renderdevice.h is
# present in KWin 6.7, whose callbacks still return void, so a 6.7 build
# selected the new signatures and failed to compile. Nothing in the installed
# headers states a version the preprocessor can compare - config-kwin.h gives
# KWIN_VERSION_STRING as a constexpr QLatin1StringView, and
# KWIN_EFFECT_API_VERSION is 0.237 in both 6.7 and master - and a version
# threshold would only be a second guess, wrong for anyone who backports.
#
# So compile the declaration and see. This cannot drift from KWin's headers,
# because it is those headers that answer.
include(CheckCXXSourceCompiles)
# KWin's effect header pulls in KCoreAddons, KConfig and Qt headers of its own,
# so the probe is given the same interface the effect itself links against
# rather than a hand-picked include path that would need a new entry every time
# KWin includes something else.
set(CMAKE_REQUIRED_LIBRARIES
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::I18n
)
set(CMAKE_REQUIRED_QUIET ON)

# Both probes answer from KWin's installed headers, and nothing in this tree
# changes when those are replaced. Their results are cache entries, so a KWin
# upgraded under an existing build directory would be answered from the cache
# and the build would select the API the previous KWin had - the wrong-API
# build this probe exists to prevent. Two of the distributions packaged here
# roll, so that upgrade is the normal case rather than an unusual one. Discard
# the previous answer and ask again on every configure; the cost is two
# try_compile runs, paid once per configure and not per build.

# A probe that answers "no" because it could not compile at all would pick an
# API silently and wrongly, which is the failure this replaces. So establish
# first that the probe can compile KWin's effect header, and refuse to guess
# when it cannot.
unset(UPSCALE_KWIN_EFFECT_HEADER_USABLE CACHE)
check_cxx_source_compiles(
    "#include \"effect/effect.h\"
    int main() { return 0; }"
    UPSCALE_KWIN_EFFECT_HEADER_USABLE
)
if(NOT UPSCALE_KWIN_EFFECT_HEADER_USABLE)
    message(
        FATAL_ERROR
        "Cannot compile KWin's effect/effect.h to determine its API generation.\n"
        "This is a broken build environment rather than an unsupported KWin: see\n"
        "the try_compile output in ${CMAKE_BINARY_DIR}/CMakeFiles/CMakeConfigureLog.yaml."
    )
endif()

# Only the return type is in question now, and the header above compiles, so a
# failure here means the callbacks return void rather than that the probe broke.
unset(UPSCALE_KWIN_PAINT_RETURNS_BOOL CACHE)
check_cxx_source_compiles(
    "#include \"effect/effect.h\"
    bool probe(KWin::Effect &effect, const KWin::RenderTarget &target,
               const KWin::RenderViewport &viewport, const KWin::Region &region,
               KWin::LogicalOutput *screen)
    {
        return effect.paintScreen(target, viewport, 0, region, screen);
    }
    int main() { return 0; }"
    UPSCALE_KWIN_PAINT_RETURNS_BOOL
)
# Later KWin hands an effect that intercepts the pointer KWin's own pointer
# events rather than Qt mouse events.
unset(UPSCALE_KWIN_POINTER_EVENTS CACHE)
check_cxx_source_compiles(
    "#include \"effect/effect.h\"
    struct Probe : KWin::Effect
    {
        void pointerButton(KWin::PointerButtonEvent *) override {}
    };
    int main() { return 0; }"
    UPSCALE_KWIN_POINTER_EVENTS
)
unset(CMAKE_REQUIRED_LIBRARIES)
unset(CMAKE_REQUIRED_QUIET)
if(UPSCALE_KWIN_POINTER_EVENTS)
    add_definitions(-DUPSCALE_POINTER_EVENT_API=1)
else()
    add_definitions(-DUPSCALE_POINTER_EVENT_API=0)
endif()
if(UPSCALE_KWIN_PAINT_RETURNS_BOOL)
    add_definitions(-DUPSCALE_RENDER_DEVICE_API=1)
else()
    add_definitions(-DUPSCALE_RENDER_DEVICE_API=0)
endif()
message(STATUS "KWin effect callbacks return bool: ${UPSCALE_KWIN_PAINT_RETURNS_BOOL}")

# Build the configuration page the way KWin does when its KCMs are enabled.
option(KWIN_BUILD_KCMS "Build the effect configuration page" ON)

if(KWIN_BUILD_KCMS)
    find_package(KF6KCMUtils ${KF_MIN_VERSION} REQUIRED)
endif()

# The two integration cases each start a nested kwin_wayland with Xwayland and
# drive a real client through it. Whether that client answers inside the
# effect's three-second validation window is a property of the machine, so they
# belong where a machine is chosen - the pull request checks, which run the
# whole suite on both architectures - and not in a distribution package build,
# which runs on whatever the builder has. debian/rules turns this off for
# exactly that reason; everywhere else it stays on.
#
# A plain variable rather than option(), so nothing is written to the cache.
# The source archive job began failing with "manifest 'build.ninja' still dirty
# after 100 tries" in the commit that introduced this switch - 201 configures in
# one job, so a genuine regeneration loop - and moving the option to this file
# did not stop it. A -D on the command line still wins, which is all
# debian/rules needs; it just no longer adds a cache entry to do it.
if(NOT DEFINED UPSCALE_SESSION_TESTS)
    set(UPSCALE_SESSION_TESTS ON)
endif()

include(KWinBuiltinEffectShim)
include(UpscaleInstrumentation)

add_subdirectory(src/plugins/upscale)
add_subdirectory(src/winedesktop)
if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()

# `make clang-format` the way every KDE project offers it. The git hook itself
# is not installed from here: ECM's kde_configure_git_pre_commit_hook writes
# .git/hooks/pre-commit, and so does `pre-commit install`, which would leave two
# systems fighting over one file. .pre-commit-config.yaml runs clang-format with
# the same .clang-format, plus the checks ECM has no hook for.
file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES src/*.cpp src/*.h)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
