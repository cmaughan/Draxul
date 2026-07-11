# ScoreView (plans/scoreview.md): draxul-scoreview is the GPU-free layout
# pipeline (Verovio engine wrapper; phase-3 SVG interpreter joins it);
# draxul-scoreview-host is the IHost + NanoVG presentation layer.
add_library(draxul-scoreview STATIC
    src/bot_player_input.cpp
    src/flow_controller.cpp
    src/score_highlight.cpp
    src/score_timemap.cpp
    src/svg_score_interpreter.cpp
    src/verovio_layout_engine.cpp
)

target_include_directories(draxul-scoreview PUBLIC
    include
)

# Verovio's library target does not export interface include dirs; mirror the
# include layout its own build uses. The NO_*_SUPPORT definitions must match
# the ones its build was configured with, since vrv headers branch on them.
target_include_directories(draxul-scoreview SYSTEM PRIVATE
    ${verovio_SOURCE_DIR}/include
    ${verovio_SOURCE_DIR}/include/vrv
    ${verovio_SOURCE_DIR}/include/json
    ${verovio_SOURCE_DIR}/include/midi
    ${verovio_SOURCE_DIR}/include/pugi
    ${verovio_SOURCE_DIR}/include/tuning-library
    ${verovio_SOURCE_DIR}/include/zip
    ${verovio_SOURCE_DIR}/libmei/dist
    ${verovio_SOURCE_DIR}/libmei/addons
)
target_compile_definitions(draxul-scoreview PRIVATE
    NO_HUMDRUM_SUPPORT
    NO_ABC_SUPPORT
    NO_PAE_SUPPORT
    NO_GABC_SUPPORT
    NO_DARMS_SUPPORT
)

target_link_libraries(draxul-scoreview
    PUBLIC draxul-types
    PRIVATE verovio tinyxml2::tinyxml2 nlohmann_json::nlohmann_json
)

add_library(draxul-scoreview-host STATIC
    src/score_host.cpp
    src/score_render_nvg.cpp
)

target_include_directories(draxul-scoreview-host PUBLIC
    include
)

target_link_libraries(draxul-scoreview-host PUBLIC
    draxul-scoreview
    draxul-notation
    draxul-host
    draxul-renderer
    draxul-nanovg
)
