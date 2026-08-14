cmake_minimum_required(VERSION 3.25)

include(cmake/Dependencies.cmake)

# SatView owns its complete product stack. Draxul links only the resulting
# native module; these private libraries keep product tests and backends small.
set(SATVIEW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/product/include")

set(SATVIEW_SGP4_SOURCE_DIR "${celestrak_sgp4_SOURCE_DIR}/cpp/SGP4/SGP4")
if(NOT EXISTS "${SATVIEW_SGP4_SOURCE_DIR}/SGP4.cpp")
    message(FATAL_ERROR "CelesTrak SGP4 source was not found at ${SATVIEW_SGP4_SOURCE_DIR}")
endif()
add_library(celestrak-sgp4 STATIC src/core/vallado_sgp4.cpp)
target_include_directories(celestrak-sgp4 SYSTEM PUBLIC "${SATVIEW_SGP4_SOURCE_DIR}")
if(MSVC)
    target_compile_options(celestrak-sgp4 PRIVATE /w)
else()
    target_compile_options(celestrak-sgp4 PRIVATE -w)
endif()

add_library(draxul-satview-core STATIC
    src/core/satview_catalog.cpp
    src/core/satview_catalog_container.cpp
    src/core/satview_config.cpp
    src/core/satview_constellation_boundary_catalog.cpp
    src/core/satview_filter.cpp
    src/core/satview_geodetic.cpp
    src/core/satview_ground_view.cpp
    src/core/satview_lunar_frame.cpp
    src/core/satview_lunar_surface_catalog.cpp
    src/core/satview_map_projection.cpp
    src/core/satview_moon_ephemeris.cpp
    src/core/satview_propagation.cpp
    src/core/satview_sky_projection.cpp
    src/core/satview_solar_system.cpp
    src/core/satview_sun_ephemeris.cpp
    src/core/satview_surface_catalog.cpp
    src/core/satview_texture_assets.cpp)
target_include_directories(draxul-satview-core PUBLIC "${SATVIEW_INCLUDE_DIR}" PRIVATE src/core ${stb_SOURCE_DIR})
target_compile_definitions(draxul-satview-core PRIVATE
    "DRAXUL_SATVIEW_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(draxul-satview-core PUBLIC draxul-types PRIVATE celestrak-sgp4 draxul-performance)

add_library(draxul-satview-scene STATIC
    src/scene/satview_constellation_catalog.cpp
    src/scene/satview_label_layout.cpp
    src/scene/satview_landscape.cpp
    src/scene/satview_star_catalog.cpp)
target_include_directories(draxul-satview-scene PUBLIC "${SATVIEW_INCLUDE_DIR}" PRIVATE src/scene)
target_link_libraries(draxul-satview-scene PUBLIC draxul-satview-core draxul-renderer PRIVATE draxul-performance)

add_library(draxul-satview-services STATIC
    src/services/satview_catalog_service.cpp
    src/services/satview_cloud_service.cpp)
target_include_directories(draxul-satview-services PUBLIC "${SATVIEW_INCLUDE_DIR}" PRIVATE src/services)
target_link_libraries(draxul-satview-services PUBLIC draxul-satview-core draxul-http PRIVATE draxul-performance)

if(APPLE)
    set(SATVIEW_RENDERER_SOURCE src/render/satview_render.mm)
else()
    set(SATVIEW_RENDERER_SOURCE src/render/satview_render_vk.cpp)
endif()
add_library(draxul-satview-renderer STATIC ${SATVIEW_RENDERER_SOURCE})
target_include_directories(draxul-satview-renderer PRIVATE src/render)
target_compile_definitions(draxul-satview-renderer PRIVATE
    "DRAXUL_SATVIEW_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
target_link_libraries(draxul-satview-renderer PUBLIC draxul-satview-scene)
target_link_libraries(draxul-satview-renderer PRIVATE draxul-performance draxul-satview-core imgui)
if(APPLE)
    find_library(SATVIEW_METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-satview-renderer PRIVATE ${SATVIEW_METAL_FRAMEWORK})
    set_source_files_properties(src/render/satview_render.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
else()
    target_link_libraries(draxul-satview-renderer PRIVATE
        draxul-vulkan-resources Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

add_library(draxul-satview-runtime STATIC
    src/runtime/satview_config_io.cpp
    src/runtime/satview_runtime.cpp
    src/runtime/satview_simulation_worker.cpp)
target_include_directories(draxul-satview-runtime PUBLIC "${SATVIEW_INCLUDE_DIR}" PRIVATE src/runtime)
target_link_libraries(draxul-satview-runtime PUBLIC
    draxul-satview-core draxul-satview-scene draxul-satview-services
    draxul-satview-renderer draxul-host draxul-renderer imgui)
target_link_libraries(draxul-satview-runtime PRIVATE draxul-config draxul-font draxul-ui SDL3::SDL3)

add_library(draxul-satview-renderer-test-internals INTERFACE)
target_include_directories(draxul-satview-renderer-test-internals INTERFACE src/render)
target_link_libraries(draxul-satview-renderer-test-internals INTERFACE draxul-satview-renderer)
add_library(draxul-satview-runtime-test-internals INTERFACE)
target_include_directories(draxul-satview-runtime-test-internals INTERFACE src/runtime)
target_link_libraries(draxul-satview-runtime-test-internals INTERFACE draxul-satview-runtime)

if(APPLE)
    set(DRAXUL_SATVIEW_PLUGIN_SOURCE src/satview_plugin_metal.mm)
else()
    set(DRAXUL_SATVIEW_PLUGIN_SOURCE src/satview_plugin_vk.cpp)
endif()
add_library(draxul-satview-plugin MODULE ${DRAXUL_SATVIEW_PLUGIN_SOURCE})
target_include_directories(draxul-satview-plugin PRIVATE "${SATVIEW_INCLUDE_DIR}" src)
target_link_libraries(draxul-satview-plugin PRIVATE
    Draxul::PluginSDK Draxul::PluginImGui
    nlohmann_json::nlohmann_json draxul-satview-runtime)
if(NOT APPLE)
    target_link_libraries(draxul-satview-plugin PRIVATE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()
set_target_properties(draxul-satview-plugin PROPERTIES PREFIX "" OUTPUT_NAME "draxul-satview")

if(APPLE)
    target_link_libraries(draxul-satview-plugin PRIVATE "-framework Metal" "-framework Foundation")
    set(SATVIEW_PLUGIN_METALLIB "${CMAKE_CURRENT_BINARY_DIR}/satview_scene.metallib")
    set(SATVIEW_PLUGIN_METAL_AIR "${CMAKE_CURRENT_BINARY_DIR}/satview_scene.air")
    add_custom_command(
        OUTPUT "${SATVIEW_PLUGIN_METALLIB}"
        COMMAND xcrun -sdk macosx metal -c "${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_scene.metal" -o "${SATVIEW_PLUGIN_METAL_AIR}"
        COMMAND xcrun -sdk macosx metallib "${SATVIEW_PLUGIN_METAL_AIR}" -o "${SATVIEW_PLUGIN_METALLIB}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_scene.metal"
        VERBATIM)
    add_custom_target(compile_satview_plugin_metal DEPENDS "${SATVIEW_PLUGIN_METALLIB}")
    add_dependencies(draxul-satview-plugin compile_satview_plugin_metal)
else()
    file(GLOB SATVIEW_GLSL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")
    set(SATVIEW_PLUGIN_SHADER_OUTPUTS)
    foreach(SATVIEW_SHADER IN LISTS SATVIEW_GLSL_SOURCES)
        get_filename_component(SATVIEW_SHADER_NAME "${SATVIEW_SHADER}" NAME)
        set(SATVIEW_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${SATVIEW_SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT "${SATVIEW_SHADER_OUTPUT}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
                "${SATVIEW_SHADER}" -o "${SATVIEW_SHADER_OUTPUT}"
            DEPENDS "${SATVIEW_SHADER}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/satview_sky_projection.glsl"
            VERBATIM)
        list(APPEND SATVIEW_PLUGIN_SHADER_OUTPUTS "${SATVIEW_SHADER_OUTPUT}")
    endforeach()
    add_custom_target(compile_satview_plugin_vulkan DEPENDS ${SATVIEW_PLUGIN_SHADER_OUTPUTS})
    add_dependencies(draxul-satview-plugin compile_satview_plugin_vulkan)
endif()

set(_satview_product_targets
    celestrak-sgp4
    draxul-satview-core
    draxul-satview-scene
    draxul-satview-services
    draxul-satview-renderer
    draxul-satview-runtime
    draxul-satview-plugin)
foreach(_target IN LISTS _satview_product_targets)
    if(COMMAND draxul_apply_sanitizers)
        draxul_apply_sanitizers(${_target})
        draxul_apply_coverage(${_target})
        draxul_apply_msvc_parallel_pdb_fix(${_target})
    endif()
endforeach()

set(_satview_shader_payload)
file(GLOB _satview_test_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/satview_*_tests.cpp")
if(APPLE)
    list(APPEND _satview_shader_payload
        "${SATVIEW_PLUGIN_METALLIB}=>shaders/satview_scene.metallib")
    set(_satview_shader_dependencies "${SATVIEW_PLUGIN_METALLIB}")
else()
    foreach(_shader IN LISTS SATVIEW_PLUGIN_SHADER_OUTPUTS)
        get_filename_component(_shader_name "${_shader}" NAME)
        list(APPEND _satview_shader_payload
            "${_shader}=>shaders/${_shader_name}")
    endforeach()
    set(_satview_shader_dependencies ${SATVIEW_PLUGIN_SHADER_OUTPUTS})
endif()

draxul_register_bundled_plugin(
    ID dev.draxul.satview
    TARGET draxul-satview-plugin
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/plugin.toml"
    FEATURE DRAXUL_ENABLE_SATVIEW
    DEPENDENCY_MODE MIGRATING
    PRODUCT_TARGETS ${_satview_product_targets}
    TEST_SOURCES ${_satview_test_sources}
    TEST_CMAKE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Tests.cmake"
    COPY_FILES ${_satview_shader_payload}
    COPY_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/assets=>assets"
    DEPENDS ${_satview_shader_dependencies})
