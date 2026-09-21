add_library(draxul-terminal-process
    src/agent_process_observer.cpp
)

if(WIN32)
    target_sources(draxul-terminal-process PRIVATE
        src/conpty_process.cpp
    )
    target_link_libraries(draxul-terminal-process PRIVATE shell32)
else()
    target_sources(draxul-terminal-process PRIVATE
        src/unix_agent_process_probe.cpp
        src/unix_pty_process.cpp
    )
endif()

target_include_directories(draxul-terminal-process PUBLIC include)

target_link_libraries(draxul-terminal-process PUBLIC
    draxul-agent
)
target_link_libraries(draxul-terminal-process PRIVATE
    draxul-performance
    draxul-types
)

# Private observer/scheduling contracts are exposed only to focused core tests.
add_library(draxul-terminal-process-test-internals INTERFACE)
target_include_directories(draxul-terminal-process-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-terminal-process-test-internals INTERFACE
    draxul-terminal-process
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
