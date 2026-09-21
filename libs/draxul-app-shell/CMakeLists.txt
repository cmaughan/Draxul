add_library(draxul-app-shell STATIC
    src/app_shell_layout.cpp
    src/chrome_layout.cpp
    src/chrome_pill.cpp
    src/fuzzy_match.cpp
    src/rename_editor.cpp
    src/split_tree.cpp
)

target_include_directories(draxul-app-shell PUBLIC include)

# Public headers expose only neutral config, Session-layout, and value records.
# Protocol bounds and timing/log instrumentation are implementation details.
target_link_libraries(draxul-app-shell PUBLIC
    draxul-config
    draxul-session-model
    draxul-types
)
target_link_libraries(draxul-app-shell PRIVATE
    draxul-performance
    draxul-protocol
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
