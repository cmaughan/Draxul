cmake_minimum_required(VERSION 3.25)

# NOTE: This target is being decomposed into stable library boundaries
# (kanban 26). draxul-satview-core has been peeled off below; scene, services,
# renderer, and host follow. Until the split completes this target holds the
# not-yet-extracted scene/services/renderer/host sources and links the
# already-extracted lower targets PUBLIC so tests and the app keep resolving
# their headers transitively.

set(SATVIEW_SOURCES
    src/satview_config_io.cpp
    src/satview_host.cpp
    src/satview_simulation_worker.cpp
)

add_library(draxul-satview STATIC ${SATVIEW_SOURCES})

target_include_directories(draxul-satview
    PUBLIC include
    PRIVATE src ${stb_SOURCE_DIR}
)

target_compile_definitions(draxul-satview PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

target_link_libraries(draxul-satview PUBLIC
    draxul-satview-core
    draxul-satview-scene
    draxul-satview-services
    draxul-satview-renderer
    draxul-types
    draxul-host
    draxul-renderer
    imgui
)
target_link_libraries(draxul-satview PRIVATE
    draxul-config
    draxul-font
    draxul-ui
)
