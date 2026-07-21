add_library(draxul-types
    src/log.cpp
    src/runtime_path.cpp
)
target_include_directories(draxul-types PUBLIC include)
target_link_libraries(draxul-types PUBLIC glm::glm)
target_link_libraries(draxul-types PRIVATE draxul-performance)
