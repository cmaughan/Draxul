cmake_minimum_required(VERSION 3.25)

set(DRAXUL_TRIANGLE_STANDALONE OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(DRAXUL_TRIANGLE_STANDALONE ON)
    project(draxul-spinning-triangle VERSION 0.2.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(APPLE)
        enable_language(OBJCXX)
        set(CMAKE_OBJCXX_STANDARD 20)
        set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
        set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
    endif()
endif()

if(NOT TARGET Draxul::PluginSDK)
    find_package(DraxulPluginSDK CONFIG REQUIRED)
endif()
if(NOT APPLE AND NOT TARGET Vulkan::Vulkan)
    find_package(Vulkan REQUIRED COMPONENTS glslc)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(DRAXUL_TRIANGLE_PLUGIN_ID dev.draxul.spinning-triangle)

if(APPLE)
    set(DRAXUL_TRIANGLE_SOURCE src/spinning_triangle_metal.mm)
else()
    set(DRAXUL_TRIANGLE_SOURCE src/spinning_triangle_vk.cpp)
endif()

add_library(draxul-spinning-triangle MODULE ${DRAXUL_TRIANGLE_SOURCE})
target_link_libraries(draxul-spinning-triangle PRIVATE
    Draxul::PluginSDK
    nlohmann_json::nlohmann_json)
set_target_properties(draxul-spinning-triangle PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-spinning-triangle")

if(APPLE)
    target_link_libraries(draxul-spinning-triangle PRIVATE
        "-framework Metal"
        "-framework Foundation")
    set(TRIANGLE_METAL_AIR
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.air)
    set(TRIANGLE_METALLIB
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.metallib)
    add_custom_command(
        OUTPUT ${TRIANGLE_METALLIB}
        COMMAND xcrun -sdk macosx metal -c
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.metal
            -o ${TRIANGLE_METAL_AIR}
        COMMAND xcrun -sdk macosx metallib
            ${TRIANGLE_METAL_AIR} -o ${TRIANGLE_METALLIB}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.metal
        VERBATIM)
    add_custom_target(compile_spinning_triangle_metal
        DEPENDS ${TRIANGLE_METALLIB})
    add_dependencies(draxul-spinning-triangle
        compile_spinning_triangle_metal)
else()
    target_link_libraries(draxul-spinning-triangle PRIVATE
        Vulkan::Vulkan)
    set(TRIANGLE_VERTEX_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.vert.spv)
    set(TRIANGLE_FRAGMENT_SPV
        ${CMAKE_CURRENT_BINARY_DIR}/spinning_triangle.frag.spv)
    add_custom_command(
        OUTPUT ${TRIANGLE_VERTEX_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.vert
            -o ${TRIANGLE_VERTEX_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.vert
        VERBATIM)
    add_custom_command(
        OUTPUT ${TRIANGLE_FRAGMENT_SPV}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.frag
            -o ${TRIANGLE_FRAGMENT_SPV}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/spinning_triangle.frag
        VERBATIM)
    add_custom_target(compile_spinning_triangle_vulkan
        DEPENDS ${TRIANGLE_VERTEX_SPV} ${TRIANGLE_FRAGMENT_SPV})
    add_dependencies(draxul-spinning-triangle
        compile_spinning_triangle_vulkan)
endif()

if(NOT DRAXUL_TRIANGLE_STANDALONE)
    if(APPLE)
        set(_triangle_payload
            "${TRIANGLE_METALLIB}=>spinning_triangle.metallib")
        set(_triangle_payload_dependencies "${TRIANGLE_METALLIB}")
    else()
        set(_triangle_payload
            "${TRIANGLE_VERTEX_SPV}=>spinning_triangle.vert.spv"
            "${TRIANGLE_FRAGMENT_SPV}=>spinning_triangle.frag.spv")
        set(_triangle_payload_dependencies
            "${TRIANGLE_VERTEX_SPV}"
            "${TRIANGLE_FRAGMENT_SPV}")
    endif()

    draxul_register_bundled_plugin(
        ID "${DRAXUL_TRIANGLE_PLUGIN_ID}"
        TARGET draxul-spinning-triangle
        MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
        COPY_FILES ${_triangle_payload}
        DEPENDS ${_triangle_payload_dependencies})
endif()
