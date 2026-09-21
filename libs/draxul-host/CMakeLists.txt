set(DRAXUL_HOST_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Neutral lifecycle, callbacks, launch records, and provider registry. Concrete
# presentation/process implementations must stay below this boundary.
add_library(draxul-host-api STATIC
    src/host_registry.cpp
)
target_include_directories(draxul-host-api PUBLIC "${DRAXUL_HOST_INCLUDE_DIR}")
target_link_libraries(draxul-host-api PUBLIC
    draxul-agent
    draxul-host-identity
    draxul-types
)
target_link_libraries(draxul-host-api PRIVATE draxul-performance)

# Shared renderer-backed grid presentation for real grid consumers.
add_library(draxul-grid-host STATIC
    src/grid_host_base.cpp
    src/unavailable_host.cpp
)
target_include_directories(draxul-grid-host PUBLIC "${DRAXUL_HOST_INCLUDE_DIR}")
target_link_libraries(draxul-grid-host PUBLIC
    draxul-grid
    draxul-host-api
    draxul-runtime-support
    draxul-types
)
target_link_libraries(draxul-grid-host PRIVATE
    draxul-font
    draxul-performance
    draxul-renderer
    draxul-window
)

# Client terminal presentation. PTY/ConPTY ownership remains exclusively in
# draxul-terminal-process and the headless server; this target has no process
# implementation dependency.
add_library(draxul-terminal-host STATIC
    src/local_terminal_host.cpp
    src/mouse_reporter.cpp
    src/remote_terminal_host.cpp
    src/selection_manager.cpp
    src/terminal_host_base.cpp
    src/terminal_surface_host_base.cpp
    src/terminal_key_encoder.cpp
)
target_include_directories(draxul-terminal-host PUBLIC "${DRAXUL_HOST_INCLUDE_DIR}")
target_link_libraries(draxul-terminal-host PUBLIC
    draxul-grid-host
    draxul-terminal-core
)
target_link_libraries(draxul-terminal-host PRIVATE
    SDL3::SDL3
    draxul-client
    draxul-performance
    draxul-window
)

# Neovim is a concrete grid host. Keep the protocol/transport implementation
# private so the host target's public contract does not expose either surface.
add_library(draxul-nvim-host STATIC
    src/host_factory.cpp
    src/nvim_host.cpp
)
target_include_directories(draxul-nvim-host PUBLIC "${DRAXUL_HOST_INCLUDE_DIR}")
target_include_directories(draxul-nvim-host PRIVATE src)
target_link_libraries(draxul-nvim-host PRIVATE
    draxul-font
    draxul-grid-host
    draxul-host-api
    draxul-nvim-protocol
    draxul-nvim-transport
    draxul-performance
    draxul-window
)

# Dynamic native-plugin adapter and platform render-pass bridge.
add_library(draxul-plugin-host STATIC
    src/plugin_host.cpp
    src/plugin_storage.cpp
)
if(APPLE)
    target_sources(draxul-plugin-host PRIVATE src/plugin_render_pass_metal.mm)
else()
    target_sources(draxul-plugin-host PRIVATE src/plugin_render_pass_vk.cpp)
    target_link_libraries(draxul-plugin-host PRIVATE
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    )
endif()
target_include_directories(draxul-plugin-host PUBLIC "${DRAXUL_HOST_INCLUDE_DIR}")
target_include_directories(draxul-plugin-host PRIVATE src)
target_link_libraries(draxul-plugin-host PUBLIC
    Draxul::PluginSDK
    draxul-host-api
)
target_link_libraries(draxul-plugin-host PRIVATE
    draxul-performance
    draxul-plugin
    draxul-renderer
    draxul-types
    nlohmann_json::nlohmann_json
)

# Compatibility aggregate for downstream callers while direct consumers move
# to the smallest owning leaf above.
add_library(draxul-host INTERFACE)
target_include_directories(draxul-host INTERFACE "${DRAXUL_HOST_INCLUDE_DIR}")
target_link_libraries(draxul-host INTERFACE
    draxul-grid-host
    draxul-host-api
    draxul-nvim-host
    draxul-plugin-host
    draxul-terminal-host
)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
