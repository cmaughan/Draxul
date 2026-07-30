add_library(draxul-host
    src/grid_host_base.cpp
    src/host_factory.cpp
    src/host_registry.cpp
    src/local_terminal_host.cpp
    src/mouse_reporter.cpp
    src/nvim_host.cpp
    src/remote_terminal_host.cpp
    src/selection_manager.cpp
    src/terminal_host_base.cpp
    src/terminal_key_encoder.cpp
    src/unavailable_host.cpp
)

if(WIN32)
    target_sources(draxul-host PRIVATE
        src/shell_host_win.cpp
    )
else()
    target_sources(draxul-host PRIVATE
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
    draxul-terminal-core
    draxul-terminal-process
    draxul-types
)
target_link_libraries(draxul-host PRIVATE
    draxul-client
    draxul-font
    draxul-performance
    draxul-window
)
