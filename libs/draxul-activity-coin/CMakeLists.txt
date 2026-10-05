# draxul-activity-coin — the TokenFu-derived 3D activity coin as an in-process
# IRenderPass. The mesh is procedural and facets are culled in the vertex
# shader, so the pass owns only a pipeline: no buffers, textures or depth.
#
# Metal compiles its embedded source at runtime (the NanoVG Metal pattern).
# Vulkan loads activity_coin.{vert,frag}.spv from the bundled shader directory;
# cmake/CompileShaders.cmake compiles the GLSL in shaders/ here.

if(APPLE)
    add_library(draxul-activity-coin
        src/activity_coin_pass_metal.mm
    )
    target_link_libraries(draxul-activity-coin
        PRIVATE "-framework Metal" "-framework Foundation"
    )
    target_compile_options(draxul-activity-coin PRIVATE -fobjc-arc)
else()
    add_library(draxul-activity-coin
        src/activity_coin_pass_vk.cpp
    )
    target_link_libraries(draxul-activity-coin
        PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
    )
endif()
target_include_directories(draxul-activity-coin
    PUBLIC include
    PRIVATE src
)
target_link_libraries(draxul-activity-coin
    PUBLIC draxul-plugin-render-support
    PRIVATE draxul-types
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
