cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(SATVIEW_SOURCES
        src/satview_catalog.cpp
        src/satview_catalog_service.cpp
        src/satview_host.cpp
        src/satview_texture_assets.cpp
        src/satview_render.mm
    )
else()
    set(SATVIEW_SOURCES
        src/satview_catalog.cpp
        src/satview_catalog_service.cpp
        src/satview_host.cpp
        src/satview_texture_assets.cpp
        src/satview_render_vk.cpp
    )
endif()

add_library(draxul-satview STATIC ${SATVIEW_SOURCES})

target_include_directories(draxul-satview
    PUBLIC include
    PRIVATE src ${stb_SOURCE_DIR}
)

target_compile_definitions(draxul-satview PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

target_link_libraries(draxul-satview PUBLIC
    draxul-types
    draxul-host
    draxul-renderer
)

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-satview PRIVATE ${METAL_FRAMEWORK})
    set_source_files_properties(src/satview_render.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
else()
    target_link_libraries(draxul-satview PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()
