cmake_minimum_required(VERSION 3.25)

add_library(draxul-citymodel STATIC
    src/treesitter_semantic_source.cpp
)

target_include_directories(draxul-citymodel
    PUBLIC include
)

target_link_libraries(draxul-citymodel PUBLIC
    draxul-treesitter
    draxul-types
)
