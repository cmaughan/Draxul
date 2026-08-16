# Narrow, same-build C++ helpers for mounted native plugins. These targets are
# statically linked into modules; Draxul::PluginSDK remains the runtime ABI.

add_library(draxul-plugin-runtime-support STATIC
    src/plugin_host_services.cpp)
target_include_directories(draxul-plugin-runtime-support PUBLIC include)
target_link_libraries(draxul-plugin-runtime-support PUBLIC
    Draxul::PluginSDK
    draxul-types)

# The plugin-runtime vocabulary (PluginRuntimeContext and friends) ships with
# the plugin SDK so out-of-tree product builds can speak it; it only needs the
# draxul-types headers that already travel with the SDK.
include(GNUInstallDirs)
install(FILES include/draxul/plugin_runtime.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/draxul"
    COMPONENT draxul-plugin-sdk)

add_library(draxul-plugin-render-support INTERFACE)
target_include_directories(draxul-plugin-render-support INTERFACE include)
target_link_libraries(draxul-plugin-render-support INTERFACE draxul-types)
if(APPLE)
    target_link_libraries(draxul-plugin-render-support INTERFACE "-framework Metal")
else()
    target_link_libraries(draxul-plugin-render-support INTERFACE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

add_library(draxul-plugin-config-support STATIC
    src/config_document.cpp)
target_include_directories(draxul-plugin-config-support PUBLIC include)
target_link_libraries(draxul-plugin-config-support
    PUBLIC draxul-types tomlplusplus::tomlplusplus
    PRIVATE draxul-performance)

add_library(draxul-plugin-tooltip-support STATIC
    src/overlay_text.cpp
    src/tooltip.cpp)
target_include_directories(draxul-plugin-tooltip-support PUBLIC include)
target_link_libraries(draxul-plugin-tooltip-support
    PUBLIC draxul-font
    PRIVATE draxul-performance)

if(NOT APPLE)
    add_library(draxul-vulkan-resources STATIC
        src/vulkan/vk_resource_helpers.cpp
        src/vulkan/vma_impl.cpp)
    target_include_directories(draxul-vulkan-resources PUBLIC include)
    target_link_libraries(draxul-vulkan-resources PRIVATE
        Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator)
endif()

add_library(Draxul::PluginSupport::Runtime ALIAS draxul-plugin-runtime-support)
add_library(Draxul::PluginSupport::Render ALIAS draxul-plugin-render-support)
add_library(Draxul::PluginSupport::Config ALIAS draxul-plugin-config-support)
add_library(Draxul::PluginSupport::Tooltip ALIAS draxul-plugin-tooltip-support)
add_library(Draxul::PluginSupport::Types ALIAS draxul-types)
add_library(Draxul::PluginSupport::ImGuiCore ALIAS draxul-imgui-core)
add_library(Draxul::PluginSupport::NanoVG ALIAS draxul-nanovg-backend)
add_library(Draxul::PluginSupport::Performance ALIAS draxul-performance)
add_library(Draxul::PluginSupport::Text ALIAS draxul-font)
add_library(Draxul::PluginSupport::Http ALIAS draxul-http)
if(TARGET draxul-vulkan-resources)
    add_library(Draxul::PluginSupport::VulkanResources ALIAS draxul-vulkan-resources)
endif()
