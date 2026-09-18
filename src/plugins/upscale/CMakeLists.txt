#######################################
# Effect

set(upscale_SOURCES
    application.cpp
    display.cpp
    eligibility.cpp
    framestatistics.cpp
    main.cpp
    modeoverride.cpp
    overlay.cpp
    scaler.cpp
    snapshot.cpp
    upscale.cpp
    upscale.qrc
)

kconfig_add_kcfg_files(upscale_SOURCES upscaleconfig.kcfgc)

# The effect's own application defaults, beside the session's other
# configuration defaults. A user's changes go to their own file of the same
# name, which KConfig layers over this one.
install(FILES kwinupscalerc DESTINATION ${KDE_INSTALL_CONFDIR})

kwin_add_builtin_effect(upscale ${upscale_SOURCES})
target_link_libraries(upscale PRIVATE
    kwin

    KF6::ConfigGui
    KF6::I18n
    Libdrm::Libdrm
)

#######################################
# Config

if (KWIN_BUILD_KCMS)
    set(kwin_upscale_config_SRCS
        upscale_config.cpp
        application.cpp
        applicationeditor.cpp
        supportinformation.cpp
    )
    kconfig_add_kcfg_files(kwin_upscale_config_SRCS upscaleconfig.kcfgc)
    kwin_add_effect_config(kwin_upscale_config ${kwin_upscale_config_SRCS})
    target_link_libraries(kwin_upscale_config
        KF6::KCMUtils
        KF6::CoreAddons
        KF6::ConfigGui
        KF6::I18n
        Qt::DBus
        Qt::Widgets
    )
endif()
