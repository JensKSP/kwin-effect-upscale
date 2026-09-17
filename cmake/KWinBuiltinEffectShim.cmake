# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Out-of-tree stand-ins for the build macros KWin defines in
# src/plugins/CMakeLists.txt (v6.3.6, lines 1-31). They exist so that
# src/plugins/upscale/CMakeLists.txt can stay exactly what KDE would carry
# inside KWin, with no "if building standalone" branches in it.
#
# Three differences to upstream, each forced by building outside the tree:
#
#  1. kwin_add_builtin_effect passes STATIC upstream, because builtin effects are
#     linked into the kwin binary. Out of tree the effect has to be a loadable
#     module, so STATIC is dropped.
#  2. Upstream strips translations out of metadata.json with a Python helper
#     (strip-effect-metadata.py). Those translations are added by KDE's
#     translation robot and only exist inside KWin, so here the file is copied.
#  3. The translation domain is this project's, not "kwin".

include(KDEInstallDirs)
include(KDEPackageAppTemplates OPTIONAL)

# In-tree the effect links against the target named "kwin". Out of tree the
# same name is made to point at the installed KWin library.
if(NOT TARGET kwin)
    add_library(kwin INTERFACE IMPORTED GLOBAL)
    target_link_libraries(kwin INTERFACE KWin::kwin)
endif()

function(kwin_strip_builtin_effect_metadata target metadata)
    set(stripped_metadata "${CMAKE_CURRENT_BINARY_DIR}/${metadata}.stripped")

    add_custom_command(
        OUTPUT ${stripped_metadata}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${metadata}" "${stripped_metadata}"
        DEPENDS ${metadata}
        COMMENT "Preparing ${metadata}.stripped..."
    )
    set_property(TARGET ${target} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${stripped_metadata})
endfunction()

macro(kwin_add_builtin_effect name)
    kcoreaddons_add_plugin(${name} SOURCES ${ARGN} INSTALL_NAMESPACE "kwin/effects/plugins")
    target_compile_definitions(${name} PRIVATE -DTRANSLATION_DOMAIN=\"kwin_effect_upscale\")
    kwin_strip_builtin_effect_metadata(${name} metadata.json)
endmacro()

function(kwin_add_effect_config name)
    list(REMOVE_ITEM ARGV ${name})
    kcoreaddons_add_plugin(${name} INSTALL_NAMESPACE "kwin/effects/configs" SOURCES ${ARGV})
    target_compile_definitions(${name} PRIVATE -DTRANSLATION_DOMAIN=\"kwin_effect_upscale\")
endfunction()
