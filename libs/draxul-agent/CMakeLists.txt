add_library(draxul-agent STATIC
    src/agent_model.cpp
    src/agent_registry.cpp
    src/agent_status.cpp
)

target_include_directories(draxul-agent PUBLIC include)
