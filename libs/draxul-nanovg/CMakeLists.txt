# draxul-nanovg is two targets with one seam between them:
#
# * draxul-nanovg-backend — the NanoVG core plus Draxul's custom Vulkan/Metal
#   rendering backends. Deliberately leaf-narrow (draxul-types only, for
#   logging and the default shader path) so it can sit on the plugin-support
#   allowlist as Draxul::PluginSupport::NanoVG and compile inside a standalone
#   product extraction with no other core library present.
# * draxul-nanovg — the in-process IRenderPass integration (INanoVGPass) used
#   by the application and render tests. It needs draxul-renderer's typed
#   render contexts, so it is never part of the plugin surface.
#
# The compiled SPIR-V shaders the Vulkan backend loads live in shaders/ here
# (single home; consumers compile them via the DRAXUL_NANOVG_SHADER_DIR target
# property). A standalone product extraction copies this whole directory and
# configures it with DRAXUL_NANOVG_BACKEND_ONLY=ON.

# The NanoVG core source. The superbuild does not fetch it ahead of us, so the
# pin lives here, beside the only code that consumes it; a copied standalone
# tree fetches the same pin.
if(NOT nanovg_SOURCE_DIR)
    include(FetchContent)
    # Pinned to a reviewed immutable upstream commit. Draxul's Vulkan and
    # Metal backends live below; no patch is applied to this fetched source.
    FetchContent_Declare(
        nanovg
        GIT_REPOSITORY https://github.com/memononen/nanovg.git
        GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353
    )
    FetchContent_MakeAvailable(nanovg)
endif()

if(APPLE)
    add_library(draxul-nanovg-backend STATIC
        backend/src/nanovg_wrapper.c
        backend/src/nanovg_mtl.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(draxul-nanovg-backend
        PRIVATE ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK}
    )
    # NanoVG Metal backend uses ARC for Objective-C memory management
    target_compile_options(draxul-nanovg-backend PRIVATE -fobjc-arc)
else()
    add_library(draxul-nanovg-backend STATIC
        backend/src/nanovg_wrapper.c
        backend/src/nanovg_vk.cpp
    )
    target_link_libraries(draxul-nanovg-backend
        PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
    )
    # The VMA implementation translation unit is deliberately NOT here: the
    # application provides its own and plugins share the one in
    # Draxul::PluginSupport::VulkanResources.
endif()
target_include_directories(draxul-nanovg-backend
    PUBLIC backend/include ${nanovg_SOURCE_DIR}/src
    PRIVATE backend/src
)
# draxul-types provides logging and the default (bundled-asset) shader root.
target_link_libraries(draxul-nanovg-backend PRIVATE draxul-types)
# Skip stb_image in nanovg.c — we only use NanoVG for vector shapes, not image
# loading. This avoids duplicate stb_image symbols with draxul-megacity.
target_compile_definitions(draxul-nanovg-backend PRIVATE NVG_NO_STB)
set_target_properties(draxul-nanovg-backend PROPERTIES
    DRAXUL_NANOVG_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

if(NOT DRAXUL_NANOVG_BACKEND_ONLY)
    if(APPLE)
        add_library(draxul-nanovg
            src/nanovg_pass_metal.mm
        )
        target_link_libraries(draxul-nanovg
            PRIVATE ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK}
        )
        target_compile_options(draxul-nanovg PRIVATE -fobjc-arc)
    else()
        add_library(draxul-nanovg
            src/nanovg_pass_vk.cpp
        )
        target_link_libraries(draxul-nanovg
            PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
        )
    endif()
    target_include_directories(draxul-nanovg
        PUBLIC include
        PRIVATE src
    )
    target_link_libraries(draxul-nanovg
        PUBLIC draxul-nanovg-backend draxul-renderer
    )
endif()
