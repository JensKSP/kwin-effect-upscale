#######################################
# Effect

set(upscale_SOURCES
    main.cpp
    upscale.cpp
)

kwin_add_builtin_effect(upscale ${upscale_SOURCES})
target_link_libraries(upscale PRIVATE
    kwin

    KF6::I18n
)
