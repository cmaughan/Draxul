cmake_minimum_required(VERSION 3.25)

add_library(draxul-codeviz-host STATIC
    src/codeviz_input_state.cpp
    src/isometric_camera.cpp
)

target_include_directories(draxul-codeviz-host PUBLIC
    include
)

target_link_libraries(draxul-codeviz-host
    PUBLIC
        draxul-codeviz-scene
        Draxul::PluginSupport::Types
    PRIVATE
        Draxul::PluginSupport::Performance
        ${DRAXUL_MEGACITY_SDL_TARGET}
)
