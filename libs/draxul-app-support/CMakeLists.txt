add_library(draxul-app-support
    src/app_config_io.cpp
    src/keybinding_parser.cpp
    src/cursor_blinker.cpp
    src/grid_rendering_pipeline.cpp
    src/render_test.cpp
    src/ui_request_worker.cpp
)

if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul-app-support PUBLIC DRAXUL_ENABLE_RENDER_TESTS)
endif()

target_include_directories(draxul-app-support PUBLIC
    include
)

target_include_directories(draxul-app-support PRIVATE
    src
)

target_compile_definitions(draxul-app-support PRIVATE
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(draxul-app-support PUBLIC
    draxul-font
    draxul-grid
    draxul-nvim
    draxul-renderer
)
