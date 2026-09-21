cmake_minimum_required(VERSION 3.25)
project(draxul_dependency_boundary_fixture LANGUAGES NONE)

if(NOT DEFINED DRAXUL_SOURCE_ROOT OR NOT DEFINED CASE)
    message(FATAL_ERROR "DRAXUL_SOURCE_ROOT and CASE are required")
endif()

include("${DRAXUL_SOURCE_ROOT}/cmake/CheckDependencyBoundaries.cmake")

add_library(allowed_leaf INTERFACE)
add_library(allowed_alias ALIAS allowed_leaf)
add_library(headless INTERFACE)
target_link_libraries(headless INTERFACE
    "$<LINK_ONLY:allowed_alias>"
    "$<BUILD_INTERFACE:$<TARGET_NAME_IF_EXISTS:allowed_leaf>>")

if(CASE STREQUAL "negative")
    add_library(forbidden_ui INTERFACE)
    add_library(intermediate INTERFACE)
    target_link_libraries(intermediate INTERFACE forbidden_ui)
    target_link_libraries(headless INTERFACE intermediate)
    draxul_reject_transitive_links(headless forbidden_ui)
elseif(CASE STREQUAL "positive")
    draxul_reject_transitive_links(headless forbidden_ui)
    draxul_get_transitive_link_closure(actual_closure headless)
    if(NOT allowed_leaf IN_LIST actual_closure)
        message(FATAL_ERROR
            "Wrapped/aliased dependency missing from closure: ${actual_closure}")
    endif()
else()
    message(FATAL_ERROR "Unknown CASE=${CASE}")
endif()
