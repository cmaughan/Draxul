if(NOT DEFINED DRAXUL_PLUGIN_NANOVG_BACKEND_TARGET)
    set(DRAXUL_PLUGIN_NANOVG_BACKEND_TARGET draxul-nanovg-backend)
endif()

if(APPLE)
    add_library(draxul-plugin-nanovg-support STATIC
        src/plugin_nanovg_pass.cpp
        src/plugin_nanovg_pass_metal.mm)
    find_library(DRAXUL_PLUGIN_NANOVG_METAL_FRAMEWORK Metal REQUIRED)
    target_link_libraries(draxul-plugin-nanovg-support PRIVATE
        ${DRAXUL_PLUGIN_NANOVG_METAL_FRAMEWORK})
    target_compile_options(draxul-plugin-nanovg-support PRIVATE -fobjc-arc)
else()
    if(NOT DEFINED DRAXUL_PLUGIN_NANOVG_VULKAN_RESOURCES_TARGET)
        set(DRAXUL_PLUGIN_NANOVG_VULKAN_RESOURCES_TARGET
            Draxul::PluginSupport::VulkanResources)
    endif()
    add_library(draxul-plugin-nanovg-support STATIC
        src/plugin_nanovg_pass.cpp
        src/plugin_nanovg_pass_vk.cpp)
    target_link_libraries(draxul-plugin-nanovg-support PRIVATE
        ${DRAXUL_PLUGIN_NANOVG_VULKAN_RESOURCES_TARGET}
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

target_include_directories(draxul-plugin-nanovg-support
    PUBLIC include
    PRIVATE src)
target_link_libraries(draxul-plugin-nanovg-support
    PUBLIC Draxul::PluginSDK ${DRAXUL_PLUGIN_NANOVG_BACKEND_TARGET})

if(NOT TARGET Draxul::PluginSupport::NanoVGPass)
    add_library(Draxul::PluginSupport::NanoVGPass ALIAS
        draxul-plugin-nanovg-support)
endif()

if(COMMAND draxul_configure_internal_targets_in_directory)
    draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
endif()
