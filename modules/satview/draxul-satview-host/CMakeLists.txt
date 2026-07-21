cmake_minimum_required(VERSION 3.25)

# draxul-satview-host — the orchestration layer: ImGui UI, SDL input, camera
# control, config load/store IO, the simulation worker, and the SatViewHost
# that drives the lower boundaries and self-registers with the host provider
# registry (register_satview_host_provider, called from app/main.cpp). This is
# the target the executable links.
#
# It PUBLIC-links the four lower SatView boundaries so the app (which includes
# only <draxul/satview/satview_host.h>) and the white-box tests resolve every
# SatView public header transitively.

add_library(draxul-satview-host STATIC
    src/satview_config_io.cpp
    src/satview_host.cpp
    src/satview_simulation_worker.cpp
)

target_include_directories(draxul-satview-host
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-satview-host PUBLIC
    draxul-satview-core
    draxul-satview-scene
    draxul-satview-services
    draxul-satview-renderer
    draxul-host
    draxul-renderer
    imgui
)
target_link_libraries(draxul-satview-host PRIVATE
    draxul-config
    draxul-font
    draxul-ui
    SDL3::SDL3
)

# Camera/controller/worker tests intentionally inspect host internals without
# making those implementation headers part of the SatView public API.
add_library(draxul-satview-host-test-internals INTERFACE)
target_include_directories(draxul-satview-host-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-satview-host-test-internals INTERFACE
    draxul-satview-host
)
