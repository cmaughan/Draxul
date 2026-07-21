cmake_minimum_required(VERSION 3.25)

# draxul-satview-renderer — the platform GPU backends. Exactly one backend
# source compiles per platform: Metal (satview_render.mm) on Apple, Vulkan
# (satview_render_vk.cpp) elsewhere. Both consume the single backend-neutral
# scene contract from draxul-satview-scene and provide SatViewScenePass's
# State/record() implementation. There are no public headers here, so no
# Metal/Vulkan type ever leaks upward; the host talks to the scene contract
# only. satview_render_vk.cpp is kept wired for CI but is not compiled on macOS.

if(APPLE)
    set(SATVIEW_RENDERER_BACKEND_SOURCES src/satview_render.mm)
else()
    set(SATVIEW_RENDERER_BACKEND_SOURCES src/satview_render_vk.cpp)
endif()

add_library(draxul-satview-renderer STATIC ${SATVIEW_RENDERER_BACKEND_SOURCES})

target_include_directories(draxul-satview-renderer
    PRIVATE src
)

target_compile_definitions(draxul-satview-renderer PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

target_link_libraries(draxul-satview-renderer PUBLIC
    draxul-satview-scene
)
target_link_libraries(draxul-satview-renderer PRIVATE
    draxul-performance
    draxul-satview-core
    imgui
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-satview-renderer PRIVATE ${METAL_FRAMEWORK})
    set_source_files_properties(src/satview_render.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    target_link_libraries(draxul-satview-renderer PRIVATE
        draxul-vulkan-resources
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()

# Renderer-math tests are cross-platform but intentionally white-box. Expose
# their header through a named test contract rather than a raw src/ include.
add_library(draxul-satview-renderer-test-internals INTERFACE)
target_include_directories(draxul-satview-renderer-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-satview-renderer-test-internals INTERFACE
    draxul-satview-renderer
)
