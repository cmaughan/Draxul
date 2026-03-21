cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(MEGACITY_SOURCES
        src/megacity_host.cpp
        src/megacity_render.mm
        src/ui_treesitter_panel.cpp
    )
else()
    set(MEGACITY_SOURCES
        src/megacity_host.cpp
        src/megacity_render_vk.cpp
        src/ui_treesitter_panel.cpp
    )
endif()

add_library(draxul-megacity STATIC ${MEGACITY_SOURCES})

target_include_directories(draxul-megacity
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(draxul-megacity PUBLIC
    draxul-types
    draxul-host
    draxul-renderer
    draxul-treesitter
    imgui
)

# Bake in the source root so the scanner can find the repo at runtime
target_compile_definitions(draxul-megacity PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-megacity PRIVATE ${METAL_FRAMEWORK})
    set_source_files_properties(src/megacity_render.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    # megacity_render_vk.cpp needs the private Vulkan headers (VkCubePass) and Vulkan itself
    target_include_directories(draxul-megacity PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/draxul-renderer/src/vulkan
    )
    target_link_libraries(draxul-megacity PRIVATE Vulkan::Vulkan)
endif()
