add_library(draxul-kanban
    src/kanban_board.cpp
    src/kanban_host.cpp
    src/kanban_layout.cpp
    src/kanban_navigation.cpp
    src/kanban_store.cpp
)

target_include_directories(draxul-kanban PUBLIC
    include
)

target_link_libraries(draxul-kanban PUBLIC
    draxul-host
    draxul-types
)

target_link_libraries(draxul-kanban PRIVATE
    SDL3::SDL3
)
