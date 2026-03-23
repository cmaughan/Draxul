add_library(draxul-types
    src/bmp.cpp
    src/log.cpp
)
target_include_directories(draxul-types PUBLIC include)
target_link_libraries(draxul-types PUBLIC glm::glm)
