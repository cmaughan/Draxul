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
    Draxul::PluginSupport::Performance
    Draxul::PluginSupport::Types
    Draxul::PluginSupport::Runtime
    Draxul::PluginSupport::Render
    Draxul::PluginSupport::Text
    Draxul::PluginSupport::Config
    Draxul::PluginSupport::Tooltip
    draxul-codeviz-scene
    draxul-codeviz-renderer
    draxul-codeviz-host
    draxul-code-semantics
    draxul-treesitter
    Draxul::PluginSupport::ImGui
    imgui
    ${DRAXUL_MEGACITY_SDL_TARGET}
)

target_link_libraries(draxul-megacity PRIVATE
    draxul-geometry
    Draxul::PluginSupport::ImGui
)

# Pure layout/cache white-box tests use a narrow internal surface while the
# production target continues to publish only include/draxul/.
add_library(draxul-megacity-test-internals INTERFACE)
target_include_directories(draxul-megacity-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-megacity-test-internals INTERFACE draxul-megacity)

