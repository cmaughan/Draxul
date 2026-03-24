if(APPLE)
    add_library(draxul-renderer
        src/renderer_state.cpp
        src/renderer_factory.cpp
        src/metal/metal_renderer.mm
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(draxul-renderer PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
    target_compile_options(draxul-renderer PRIVATE -Wno-deprecated-declarations)
else()
    add_library(draxul-renderer
        src/renderer_state.cpp
        src/renderer_factory.cpp
        src/vulkan/vk_renderer.cpp
        src/vulkan/vk_context.cpp
        src/vulkan/vk_pipeline.cpp
        src/vulkan/vk_buffers.cpp
        src/vulkan/vk_atlas.cpp
        src/vulkan/vk_cube_pass.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_link_libraries(draxul-renderer PRIVATE
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
    )
endif()

target_include_directories(draxul-renderer PUBLIC include)
target_include_directories(draxul-renderer PRIVATE src)
target_link_libraries(draxul-renderer PUBLIC draxul-types draxul-ui draxul-window SDL3::SDL3 imgui)
