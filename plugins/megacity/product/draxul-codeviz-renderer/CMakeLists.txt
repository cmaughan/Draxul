cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(CODEVIZ_RENDERER_PLATFORM_SOURCES
        src/codeviz_render.mm
        src/codeviz_metal_resources.mm
    )
else()
    set(CODEVIZ_RENDERER_PLATFORM_SOURCES
        src/codeviz_render_vk.cpp
        src/codeviz_vk_resources.cpp
    )
endif()

# White-box renderer tests use this explicit boundary instead of adding raw
# private source includes to the focused test executable.
add_library(draxul-codeviz-renderer-test-internals INTERFACE)
target_include_directories(draxul-codeviz-renderer-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-codeviz-renderer-test-internals INTERFACE
    draxul-codeviz-renderer
)

add_library(draxul-codeviz-renderer STATIC
    src/codeviz_material_library.cpp
    src/mesh_library.cpp
    src/shadow_cascade.cpp
    ${CODEVIZ_RENDERER_PLATFORM_SOURCES}
)

target_include_directories(draxul-codeviz-renderer
    PUBLIC include
    PRIVATE src
    PRIVATE ${stb_SOURCE_DIR}
)

target_link_libraries(draxul-codeviz-renderer
    PUBLIC
        draxul-codeviz-scene
        draxul-renderer
    PRIVATE
        draxul-performance
        draxul-geometry
        imgui
)

target_compile_definitions(draxul-codeviz-renderer PRIVATE
    "DRAXUL_MEGACITY_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}/../..\""
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-codeviz-renderer PRIVATE ${METAL_FRAMEWORK})
    set_source_files_properties(src/codeviz_render.mm src/codeviz_metal_resources.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    target_link_libraries(draxul-codeviz-renderer PRIVATE
        draxul-vulkan-resources
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()
