cmake_minimum_required(VERSION 3.25)

if(APPLE)
    set(SATVIEW_SOURCES
        src/satview_catalog.cpp
        src/satview_catalog_service.cpp
        src/satview_filter.cpp
        src/satview_host.cpp
        src/satview_propagation.cpp
        src/satview_simulation_worker.cpp
        src/satview_texture_assets.cpp
        src/satview_render.mm
    )
else()
    set(SATVIEW_SOURCES
        src/satview_catalog.cpp
        src/satview_catalog_service.cpp
        src/satview_filter.cpp
        src/satview_host.cpp
        src/satview_propagation.cpp
        src/satview_simulation_worker.cpp
        src/satview_texture_assets.cpp
        src/satview_render_vk.cpp
    )
endif()

set(SATVIEW_SGP4_SOURCE_DIR "${celestrak_sgp4_SOURCE_DIR}/cpp/SGP4/SGP4")
if(NOT EXISTS "${SATVIEW_SGP4_SOURCE_DIR}/SGP4.cpp")
    message(FATAL_ERROR "CelesTrak SGP4 source was not found at ${SATVIEW_SGP4_SOURCE_DIR}")
endif()

add_library(celestrak-sgp4 STATIC src/vallado_sgp4.cpp)
target_include_directories(celestrak-sgp4 SYSTEM PUBLIC "${SATVIEW_SGP4_SOURCE_DIR}")
if(MSVC)
    target_compile_options(celestrak-sgp4 PRIVATE /w)
else()
    target_compile_options(celestrak-sgp4 PRIVATE -w)
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
    imgui
)
target_link_libraries(draxul-satview PRIVATE celestrak-sgp4 draxul-ui)

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
