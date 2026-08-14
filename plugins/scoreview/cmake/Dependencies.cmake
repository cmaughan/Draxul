include(FetchContent)

if(NOT APPLE)
    find_package(Vulkan REQUIRED COMPONENTS glslc)
    FetchContent_Declare(VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.1.0
        GIT_SHALLOW TRUE)
    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
endif()

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.8
    GIT_SHALLOW TRUE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
    GIT_SHALLOW TRUE)
set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(tinyxml2)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353)
FetchContent_MakeAvailable(nanovg)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.8-docking
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
if(MSVC)
    target_compile_options(imgui PRIVATE /w)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

FetchContent_Declare(verovio
    GIT_REPOSITORY https://github.com/rism-digital/verovio.git
    GIT_TAG version-6.2.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake)
set(BUILD_AS_LIBRARY ON CACHE BOOL "" FORCE)
set(NO_HUMDRUM_SUPPORT ON CACHE BOOL "" FORCE)
set(NO_ABC_SUPPORT ON CACHE BOOL "" FORCE)
set(NO_PAE_SUPPORT ON CACHE BOOL "" FORCE)
set(NO_GABC_SUPPORT ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(verovio)
# Upstream rewrites git_commit.h with a timestamp during every configure. The
# source is pinned, so preserve the first generated header and keep subsequent
# standalone extraction builds incremental.
file(WRITE ${verovio_SOURCE_DIR}/tools/get_git_commit.sh
"#!/usr/bin/env sh
cd ..
output=\"./include/vrv/git_commit.h\"
if [ ! -f \"$output\" ]; then
    echo \"#define GIT_COMMIT \\\"-draxul-pinned\\\"\" > $output
fi
")
if(MSVC)
    target_compile_options(verovio PRIVATE /w)
    set_target_properties(verovio PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    target_compile_options(verovio PRIVATE -w)
endif()

FetchContent_Declare(kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG 131.1.0
    GIT_SHALLOW TRUE)
set(KISSFFT_DATATYPE float CACHE STRING "" FORCE)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_TEST OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(kissfft)

FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    GIT_SHALLOW TRUE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rtmidi)
file(READ ${rtmidi_SOURCE_DIR}/RtMidi.cpp _draxul_rtmidi_src)
string(REPLACE
    "getCoreMidiClientSingleton(const std::string& clientName) throw()"
    "getCoreMidiClientSingleton(const std::string& clientName)"
    _draxul_rtmidi_src "${_draxul_rtmidi_src}")
file(WRITE ${rtmidi_SOURCE_DIR}/RtMidi.cpp "${_draxul_rtmidi_src}")
unset(_draxul_rtmidi_src)
if(MSVC)
    target_compile_options(rtmidi PRIVATE /w)
else()
    target_compile_options(rtmidi PRIVATE -w)
endif()

FetchContent_Declare(tinysoundfont
    GIT_REPOSITORY https://github.com/schellingb/TinySoundFont.git
    GIT_TAG fbc913531b85f5707f49115110bb86b1cd583885)
FetchContent_MakeAvailable(tinysoundfont)
if(NOT TARGET tinysoundfont)
    add_library(tinysoundfont INTERFACE)
    target_include_directories(tinysoundfont SYSTEM INTERFACE
        ${tinysoundfont_SOURCE_DIR})
endif()

# Runtime package asset. Keep this product-owned so a copied ScoreView tree can
# stage the same default instrument as the in-repository bundle.
FetchContent_Declare(ydp_grand_piano
    URL https://freepats.zenvoid.org/Piano/YDP-GrandPiano/YDP-GrandPiano-SF2-20160804.tar.bz2
    URL_HASH SHA256=d243dc3e182a60df2a16e92828c1821cf3eb5748b45e2e2bdcfa9cf7af056026
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(ydp_grand_piano)
