add_library(draxul-kanban
    src/kanban_board.cpp
    src/kanban_layout.cpp
    src/kanban_navigation.cpp
    src/kanban_store.cpp
)

target_include_directories(draxul-kanban PUBLIC
    include
)

target_link_libraries(draxul-kanban PUBLIC
    draxul-types
)

target_link_libraries(draxul-kanban PRIVATE
    SDL3::SDL3
)

add_library(draxul-kanban-host STATIC
    src/kanban_host.cpp
)

target_include_directories(draxul-kanban-host PUBLIC
    include
)

target_link_libraries(draxul-kanban-host PUBLIC
    draxul-kanban
    draxul-grid-host
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
