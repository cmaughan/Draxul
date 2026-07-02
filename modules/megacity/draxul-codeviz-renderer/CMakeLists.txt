cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(CODEVIZ_RENDERER_PLATFORM_SOURCES
        src/codeviz_render.mm
    )
else()
    set(CODEVIZ_RENDERER_PLATFORM_SOURCES
        src/codeviz_render_vk.cpp
    )
endif()

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
        draxul-geometry
        imgui
)

target_compile_definitions(draxul-codeviz-renderer PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-codeviz-renderer PRIVATE ${METAL_FRAMEWORK})
    set_source_files_properties(src/codeviz_render.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    target_link_libraries(draxul-codeviz-renderer PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()
