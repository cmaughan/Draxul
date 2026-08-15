cmake_minimum_required(VERSION 3.25)
project(draxul-plugin-target-suffix-test LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
include(DraxulPlugins)

add_library(test-plugin MODULE plugin.cpp)
set_target_properties(test-plugin PROPERTIES PREFIX "" OUTPUT_NAME "test-plugin")

draxul_register_bundled_plugin(
    ID dev.draxul.test
    TARGET test-plugin
    MANIFEST "${CMAKE_CURRENT_LIST_DIR}/plugin.toml")

get_target_property(_suffix test-plugin SUFFIX)
if(APPLE AND NOT _suffix STREQUAL ".dylib")
    message(FATAL_ERROR
        "macOS plugin target suffix must be .dylib to match plugin.toml; got '${_suffix}'")
endif()
