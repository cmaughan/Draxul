if(APPLE)
    add_library(draxul-score-canvas STATIC
        src/nanovg_wrapper.c
        src/nanovg_mtl.mm
        src/nanovg_pass_metal.mm
    )
    find_library(SCORE_CANVAS_METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-score-canvas PRIVATE
        ${SCORE_CANVAS_METAL_FRAMEWORK})
    target_compile_options(draxul-score-canvas PRIVATE -fobjc-arc)
else()
    add_library(draxul-score-canvas STATIC
        src/nanovg_wrapper.c
        src/nanovg_vk.cpp
        src/nanovg_pass_vk.cpp
        src/vma_impl.cpp
    )
    target_link_libraries(draxul-score-canvas PRIVATE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

target_include_directories(draxul-score-canvas
    PUBLIC include ${nanovg_SOURCE_DIR}/src
    PRIVATE src)
target_link_libraries(draxul-score-canvas PUBLIC Draxul::PluginSDK)
target_link_libraries(draxul-score-canvas PRIVATE draxul-score-runtime-support)
target_compile_definitions(draxul-score-canvas PRIVATE NVG_NO_STB)
