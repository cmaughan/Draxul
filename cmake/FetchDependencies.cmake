include(FetchContent)

# SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.8
    GIT_SHALLOW TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# Vulkan-specific dependencies (not needed on Apple/Metal)
if(NOT APPLE)
    # vk-bootstrap
    FetchContent_Declare(
        vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG v1.3.283
        GIT_SHALLOW TRUE
    )
    set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(vk-bootstrap)

    # VulkanMemoryAllocator
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.1.0
        GIT_SHALLOW TRUE
    )
    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
endif()

# libpng (macOS only — required for FreeType to decode sbix/PNG color emoji)
if(APPLE)
    FetchContent_Declare(
        libpng
        GIT_REPOSITORY https://github.com/pnggroup/libpng.git
        GIT_TAG v1.6.43
        GIT_SHALLOW TRUE
    )
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libpng)
endif()

# FreeType
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3
    GIT_SHALLOW TRUE
)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
# Always disable PNG discovery — on Apple we wire it in manually below
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)
# On Apple: manually enable PNG in FreeType using our fetched libpng.
# We bypass find_package(PNG) entirely to avoid raw-path link issues.
if(APPLE)
    target_compile_definitions(freetype PRIVATE FT_CONFIG_OPTION_USE_PNG)
    target_link_libraries(freetype PRIVATE png_static)
    target_include_directories(freetype PRIVATE
        "${libpng_SOURCE_DIR}"
        "${libpng_BINARY_DIR}")
    # pnglibconf.h is generated at build time; ensure it exists before FreeType compiles
    add_dependencies(freetype pnglibconf_h)
endif()

# HarfBuzz
FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 10.2.0
    GIT_SHALLOW TRUE
)
set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(harfbuzz)
# Suppress warnings from HarfBuzz's macOS SDK headers (deprecated CoreText/QD types)
target_compile_options(harfbuzz PRIVATE -w)

# MPack
FetchContent_Declare(
    mpack
    GIT_REPOSITORY https://github.com/ludocode/mpack.git
    GIT_TAG v1.1.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(mpack)

# Create mpack library target
file(GLOB MPACK_SOURCES ${mpack_SOURCE_DIR}/src/mpack/*.c)
add_library(mpack_lib STATIC ${MPACK_SOURCES})
target_include_directories(mpack_lib PUBLIC ${mpack_SOURCE_DIR}/src/mpack)
target_compile_definitions(mpack_lib PUBLIC MPACK_EXTENSIONS=1)

# toml++
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tomlplusplus)
include_directories(${tomlplusplus_SOURCE_DIR}/include)

# Catch2 (test framework)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.8-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui SHARED
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# tree-sitter (core parsing library)
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tree_sitter)

add_library(tree_sitter_core STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_core PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
)
target_compile_options(tree_sitter_core PRIVATE -w)

# tree-sitter-cpp grammar
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tree_sitter_cpp)

add_library(tree_sitter_cpp_grammar STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_cpp_grammar PRIVATE
    ${tree_sitter_SOURCE_DIR}/lib/include
    ${tree_sitter_cpp_SOURCE_DIR}/src
)
target_compile_options(tree_sitter_cpp_grammar PRIVATE -w)
