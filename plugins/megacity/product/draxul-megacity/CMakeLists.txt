cmake_minimum_required(VERSION 3.25)

set(MEGACITY_SOURCES
    src/biology_builder.cpp
    src/building_tooltip.cpp
    src/city_builder.cpp
    src/city_picking.cpp
    src/city_selection.cpp
    src/lcov_coverage.cpp
    src/live_city_metrics.cpp
    src/megacity_camera_input.cpp
    src/megacity_code_config.cpp
    src/megacity_host_panels.cpp
    src/metrics_overlay_controller.cpp
    src/scene_snapshot_builder.cpp
    src/scene_publication.cpp
    src/semantic_city_layout.cpp
    src/semantic_source_controller.cpp
    src/sign_label_atlas.cpp
    src/static_mesh_family_cache.cpp
    src/megacity_host.cpp
    src/ui_city_map_panel.cpp
    src/ui_treesitter_panel.cpp
)

add_library(draxul-megacity STATIC ${MEGACITY_SOURCES})

target_include_directories(draxul-megacity
    PUBLIC include
    PRIVATE src
    PRIVATE ${stb_SOURCE_DIR}
)

target_link_libraries(draxul-megacity PUBLIC
    draxul-performance
    draxul-types
    draxul-host
    draxul-renderer
    draxul-font
    draxul-config
    draxul-codeviz-scene
    draxul-codeviz-renderer
    draxul-codeviz-host
    draxul-code-semantics
    draxul-treesitter
    draxul-gui
    imgui
)

target_link_libraries(draxul-megacity PRIVATE
    draxul-geometry
    draxul-ui
)

# Bake in the source root so the scanner can find the repo at runtime.
target_compile_definitions(draxul-megacity PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

# Pure layout/cache white-box tests use a narrow internal surface while the
# production target continues to publish only include/draxul/.
add_library(draxul-megacity-test-internals INTERFACE)
target_include_directories(draxul-megacity-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-megacity-test-internals INTERFACE draxul-megacity)

