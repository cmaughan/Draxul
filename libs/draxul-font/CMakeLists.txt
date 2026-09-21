add_library(draxul-font
    src/box_drawing.cpp
    src/cluster_blit.cpp
    src/font_manager.cpp
    src/font_resolver.cpp
    src/glyph_cache.cpp
    src/rich_text_service.cpp
    src/text_atlas_builder.cpp
    src/text_shaper.cpp
    src/text_service.cpp
)
target_include_directories(draxul-font PUBLIC include)
target_link_libraries(draxul-font
    PUBLIC draxul-types
    PRIVATE draxul-performance freetype harfbuzz
)

# White-box font tests deliberately consume private implementation headers.
# Keeping their include paths and native compile requirements on a test-only
# interface prevents those details leaking through draxul-font's public API.
add_library(draxul-font-test-internals INTERFACE)
target_include_directories(draxul-font-test-internals INTERFACE src)
target_link_libraries(draxul-font-test-internals INTERFACE
    draxul-font freetype harfbuzz)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
