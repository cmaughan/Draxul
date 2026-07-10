# Semantic score model + MusicXML importer (plans/scoreview.md, phase 1).
# Pure symbolic library: no renderer, host, or Verovio dependencies.
add_library(draxul-notation STATIC
    src/musicxml_importer.cpp
)

target_include_directories(draxul-notation PUBLIC
    include
)

target_link_libraries(draxul-notation PRIVATE
    tinyxml2::tinyxml2
)
