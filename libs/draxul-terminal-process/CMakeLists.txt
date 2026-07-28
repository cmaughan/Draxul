add_library(draxul-terminal-process)

if(WIN32)
    target_sources(draxul-terminal-process PRIVATE
        src/conpty_process.cpp
    )
    target_link_libraries(draxul-terminal-process PRIVATE shell32)
else()
    target_sources(draxul-terminal-process PRIVATE
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
