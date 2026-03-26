add_library(draxul-config
    src/app_config_io.cpp
    src/config_document.cpp
    src/keybinding_parser.cpp
)

target_include_directories(draxul-config PUBLIC
    include
)

target_compile_definitions(draxul-config PRIVATE
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(draxul-config PUBLIC
    draxul-font
    draxul-renderer
    draxul-types
    draxul-window
)

target_link_libraries(draxul-config PRIVATE
    SDL3::SDL3
)
