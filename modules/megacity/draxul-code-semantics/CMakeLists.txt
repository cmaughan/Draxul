cmake_minimum_required(VERSION 3.25)

add_library(draxul-code-semantics STATIC
    src/code_semantic_model.cpp
    src/module_path_resolver.cpp
)

target_include_directories(draxul-code-semantics
    PUBLIC include
)

target_link_libraries(draxul-code-semantics PUBLIC
    draxul-treesitter
    draxul-types
)
