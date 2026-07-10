# ScoreView music score host (plans/scoreview.md).
# Phase 0: host shell + NanoVG placeholder scene. Later phases add the
# GPU-free draxul-scoreview layout/interpreter library alongside this target.
add_library(draxul-scoreview-host STATIC
    src/score_host.cpp
)

target_include_directories(draxul-scoreview-host PUBLIC
    include
)

target_link_libraries(draxul-scoreview-host PUBLIC
    draxul-host
    draxul-renderer
    draxul-nanovg
)
