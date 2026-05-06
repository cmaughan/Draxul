set(DRAXUL_RENDERER_PUBLIC_DEPS
    draxul-types
    draxul-window
    SDL3::SDL3
    imgui
)
set(DRAXUL_IMGUI_BACKEND_WARNING_OPTIONS
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/w>"
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang,GNU>:-w>"
    "$<$<COMPILE_LANG_AND_ID:OBJCXX,Clang,AppleClang,GNU>:-w>"
)

add_library(draxul-renderer-core OBJECT
    src/renderer_state.cpp
)
target_include_directories(draxul-renderer-core
    PUBLIC include
    PRIVATE src
)
target_link_libraries(draxul-renderer-core PUBLIC ${DRAXUL_RENDERER_PUBLIC_DEPS})

if(APPLE)
    set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
        PROPERTIES COMPILE_OPTIONS "${DRAXUL_IMGUI_BACKEND_WARNING_OPTIONS}"
    )
    add_library(draxul-renderer-metal OBJECT
        src/metal/metal_renderer.mm
        ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_include_directories(draxul-renderer-metal
        PUBLIC include
        PRIVATE src
    )
    target_link_libraries(draxul-renderer-metal PRIVATE
        ${DRAXUL_RENDERER_PUBLIC_DEPS}
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
    target_compile_options(draxul-renderer-metal PRIVATE -Wno-deprecated-declarations)
    set(DRAXUL_RENDERER_BACKEND_OBJECTS
        $<TARGET_OBJECTS:draxul-renderer-metal>
    )
    set(DRAXUL_RENDERER_BACKEND_LINK_LIBS
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
else()
    set_source_files_properties(${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
        PROPERTIES COMPILE_OPTIONS "${DRAXUL_IMGUI_BACKEND_WARNING_OPTIONS}"
    )
    add_library(draxul-renderer-vulkan OBJECT
        src/vulkan/vk_renderer.cpp
        src/vulkan/vk_context.cpp
        src/vulkan/vk_pipeline.cpp
        src/vulkan/vk_buffers.cpp
        src/vulkan/vk_atlas.cpp
        src/vulkan/vk_cube_pass.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(draxul-renderer-vulkan
        PUBLIC include
        PRIVATE src
    )
    target_link_libraries(draxul-renderer-vulkan PRIVATE
        ${DRAXUL_RENDERER_PUBLIC_DEPS}
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
    )
    set(DRAXUL_RENDERER_BACKEND_OBJECTS
        $<TARGET_OBJECTS:draxul-renderer-vulkan>
    )
    set(DRAXUL_RENDERER_BACKEND_LINK_LIBS
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
    )
endif()

add_library(draxul-renderer
    src/renderer_factory.cpp
    $<TARGET_OBJECTS:draxul-renderer-core>
    ${DRAXUL_RENDERER_BACKEND_OBJECTS}
)

target_include_directories(draxul-renderer
    PUBLIC include
    PRIVATE src
)
target_link_libraries(draxul-renderer
    PUBLIC ${DRAXUL_RENDERER_PUBLIC_DEPS}
    PRIVATE ${DRAXUL_RENDERER_BACKEND_LINK_LIBS}
)
