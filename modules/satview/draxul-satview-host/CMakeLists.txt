cmake_minimum_required(VERSION 3.25)

# draxul-satview-runtime — the plugin-owned orchestration layer: ImGui UI,
# SDL input, camera control, config IO, and the simulation worker.
#
# It PUBLIC-links the lower SatView boundaries so the dynamic module and
# white-box runtime tests resolve every SatView public header transitively.

add_library(draxul-satview-runtime STATIC
    src/satview_config_io.cpp
    src/satview_runtime.cpp
    src/satview_simulation_worker.cpp
)

target_include_directories(draxul-satview-runtime
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-satview-runtime PUBLIC
    draxul-satview-core
    draxul-satview-scene
    draxul-satview-services
    draxul-satview-renderer
    draxul-host
    draxul-renderer
    imgui
)
target_link_libraries(draxul-satview-runtime PRIVATE
    draxul-config
    draxul-font
    draxul-ui
    SDL3::SDL3
)

# Camera/controller/worker tests intentionally inspect runtime internals without
# making those implementation headers part of the SatView public API.
add_library(draxul-satview-runtime-test-internals INTERFACE)
target_include_directories(draxul-satview-runtime-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-satview-runtime-test-internals INTERFACE
    draxul-satview-runtime
)
