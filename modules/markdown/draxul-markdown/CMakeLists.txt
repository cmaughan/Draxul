add_library(draxul-markdown
    src/markdown_document.cpp
    src/markdown_front_matter.cpp
    src/markdown_draw_list.cpp
    src/markdown_layout.cpp
    src/markdown_parser_md4c.cpp
    src/markdown_scroll.cpp
    src/markdown_theme.cpp
)

target_include_directories(draxul-markdown PUBLIC
    include
)

target_include_directories(draxul-markdown SYSTEM PRIVATE
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(draxul-markdown
    PUBLIC draxul-font
    PRIVATE md4c
)

if(APPLE)
    set(DRAXUL_MARKDOWN_HOST_PLATFORM_SOURCE
        src/markdown_render_pass_metal.mm
    )
else()
    set(DRAXUL_MARKDOWN_HOST_PLATFORM_SOURCE
        src/markdown_render_pass_vk.cpp
    )
endif()

add_library(draxul-markdown-host STATIC
    src/markdown_host.cpp
    src/markdown_navigation.cpp
    src/markdown_render_pass.cpp
    ${DRAXUL_MARKDOWN_HOST_PLATFORM_SOURCE}
)

target_include_directories(draxul-markdown-host PUBLIC
    include
)

target_link_libraries(draxul-markdown-host PUBLIC
    draxul-markdown
    draxul-host
    draxul-renderer
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(draxul-markdown-host PRIVATE
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
    set_source_files_properties(src/markdown_render_pass_metal.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    target_link_libraries(draxul-markdown-host PRIVATE
        draxul-vulkan-resources
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()
