add_library(draxul-host
    src/grid_host_base.cpp
    src/host_factory.cpp
    src/host_registry.cpp
    src/local_terminal_host.cpp
    src/mouse_reporter.cpp
    src/plugin_host.cpp
    src/nvim_host.cpp
    src/remote_terminal_host.cpp
    src/selection_manager.cpp
    src/terminal_host_base.cpp
    src/terminal_key_encoder.cpp
    src/unavailable_host.cpp
)

if(APPLE)
    target_sources(draxul-host PRIVATE src/plugin_render_pass_metal.mm)
else()
    target_sources(draxul-host PRIVATE src/plugin_render_pass_vk.cpp)
    target_link_libraries(draxul-host PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()

target_include_directories(draxul-host PUBLIC include)

target_link_libraries(draxul-host PUBLIC
    draxul-agent
    draxul-grid
    draxul-host-identity
    draxul-plugin
    draxul-nvim
    draxul-renderer
    draxul-runtime-support
    draxul-terminal-core
    draxul-types
)
target_link_libraries(draxul-host PRIVATE
    draxul-client
    draxul-font
    draxul-performance
    draxul-window
    nlohmann_json::nlohmann_json
)
