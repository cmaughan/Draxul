cmake_minimum_required(VERSION 3.25)

add_library(draxul-treesitter STATIC
    src/treesitter.cpp
)

target_include_directories(draxul-treesitter PUBLIC include)

target_link_libraries(draxul-treesitter PUBLIC
    tree_sitter_core
    tree_sitter_cpp_grammar
)
