add_library(draxul-agent STATIC
    src/agent_model.cpp
)

target_include_directories(draxul-agent PUBLIC include)
