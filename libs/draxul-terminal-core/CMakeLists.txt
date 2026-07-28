add_library(draxul-terminal-core
    src/alt_screen_manager.cpp
    src/scrollback_buffer.cpp
    src/terminal_core.cpp
    src/terminal_core_csi.cpp
    src/terminal_sgr.cpp
    src/terminal_snapshot.cpp
    src/vt_parser.cpp
)

target_include_directories(draxul-terminal-core PUBLIC include)

target_link_libraries(draxul-terminal-core
    PUBLIC
        draxul-grid
        draxul-types
    PRIVATE
        draxul-performance
)
