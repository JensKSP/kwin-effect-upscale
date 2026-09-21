#######################################
# Effect

set(upscale_SOURCES
    application.cpp
    autorequest.cpp
    display.cpp
    eligibility.cpp
    refusaltext.cpp
    framestatistics.cpp
    main.cpp
    modeoverride.cpp
    legacysettings.cpp
    settings.cpp
    waylandscale.cpp
    observation.cpp
    overlay.cpp
    placement.cpp
    scaler.cpp
    snapshot.cpp
    snapshot_metrics.cpp
    upscale.cpp
    x11geometry.cpp
    x11input.cpp
    x11resolution.cpp
    x11resolution_events.cpp
    x11resolution_present.cpp
    x11resolution_validate.cpp
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
    XCB::XCB
    XCB::RANDR
)

#######################################
# Config

if (KWIN_BUILD_KCMS)
    set(kwin_upscale_config_SRCS
        upscale_config.cpp
        displaycontrols.cpp
        resolutionchoice.cpp
        application.cpp
        legacysettings.cpp
        settings.cpp
        settingcontrols.cpp
        methodcontrols.cpp
        applicationeditor.cpp
        placement.cpp
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
