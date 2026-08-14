cmake_minimum_required(VERSION 3.25)

add_library(draxul-codeviz-scene STATIC
    src/codeviz_scene_sort.cpp
    src/codeviz_scene_world.cpp
)

target_include_directories(draxul-codeviz-scene PUBLIC
    include
)

target_link_libraries(draxul-codeviz-scene PUBLIC
    draxul-geometry
    EnTT::EnTT
)
target_link_libraries(draxul-codeviz-scene PRIVATE Draxul::PluginSupport::Performance)
