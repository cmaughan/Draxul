include(FetchContent)

# Product-specific semantic-model dependencies. None of these targets exist
# when the MegaCity submodule is disabled or absent.
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.14.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(EnTT)
get_target_property(_entt_inc EnTT INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(EnTT PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_entt_inc}")

FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tree_sitter)

add_library(tree_sitter_core STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c)
target_include_directories(tree_sitter_core PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include)
if(MSVC)
    target_compile_options(tree_sitter_core PRIVATE /w)
else()
    target_compile_options(tree_sitter_core PRIVATE -w)
endif()

FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake-no-add-subdirectory)
FetchContent_MakeAvailable(tree_sitter_cpp)

add_library(tree_sitter_cpp_grammar STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c)
target_include_directories(tree_sitter_cpp_grammar PRIVATE
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_cpp_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(tree_sitter_cpp_grammar PRIVATE /w)
else()
    target_compile_options(tree_sitter_cpp_grammar PRIVATE -w)
endif()
