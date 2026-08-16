add_library(draxul-agent STATIC
    src/agent_discovery.cpp
    src/agent_model.cpp
    src/agent_registry.cpp
    src/agent_status.cpp
)

target_include_directories(draxul-agent PUBLIC include)
target_link_libraries(draxul-agent PRIVATE draxul-types)
