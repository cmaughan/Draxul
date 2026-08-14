add_library(draxul-config
    src/app_config_io.cpp
    src/config_document_core.cpp
    src/config_schema.cpp
    src/keybinding_parser.cpp
)

target_include_directories(draxul-config PUBLIC
    include
)

target_compile_definitions(draxul-config PRIVATE
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(draxul-config PUBLIC
    draxul-plugin-config-support
    draxul-types
    tomlplusplus::tomlplusplus
)

target_link_libraries(draxul-config PRIVATE
    draxul-performance
    SDL3::SDL3
)
