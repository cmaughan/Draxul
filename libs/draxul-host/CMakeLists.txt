add_library(draxul-host
    src/alt_screen_manager.cpp
    src/grid_host_base.cpp
    src/host_factory.cpp
    src/host_registry.cpp
    src/local_terminal_host.cpp
    src/mouse_reporter.cpp
    src/nvim_host.cpp
    src/scrollback_buffer.cpp
    src/selection_manager.cpp
    src/terminal_host_base.cpp
    src/terminal_host_base_csi.cpp
    src/terminal_key_encoder.cpp
    src/terminal_sgr.cpp
    src/vt_parser.cpp
)

if(WIN32)
    target_sources(draxul-host PRIVATE
        src/conpty_process.cpp
        src/shell_host_win.cpp
    )
    target_link_libraries(draxul-host PRIVATE shell32)
else()
    target_sources(draxul-host PRIVATE
        src/unix_pty_process.cpp
        src/shell_host_unix.cpp
    )
endif()

target_include_directories(draxul-host PUBLIC include)

target_link_libraries(draxul-host PUBLIC
    draxul-agent
    draxul-grid
    draxul-host-identity
    draxul-nvim
    draxul-renderer
    draxul-runtime-support
    draxul-types
)
target_link_libraries(draxul-host PRIVATE
    draxul-font
    draxul-performance
    draxul-window
)
