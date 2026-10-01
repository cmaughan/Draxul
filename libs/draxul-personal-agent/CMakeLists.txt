add_library(draxul-personal-agent src/personal_agent_store.cpp)
target_include_directories(draxul-personal-agent PUBLIC include)
target_link_libraries(draxul-personal-agent PRIVATE tomlplusplus::tomlplusplus draxul-types)
draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
