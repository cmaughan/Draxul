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
    PUBLIC draxul-types freetype harfbuzz
    PRIVATE draxul-performance
)
