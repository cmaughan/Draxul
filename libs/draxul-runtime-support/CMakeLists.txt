add_library(draxul-runtime-support
    src/cursor_blinker.cpp
    src/grid_rendering_pipeline.cpp
    src/pane_print.cpp
    src/session_attach.cpp
    src/system_resource_monitor.cpp
    src/ui_request_worker.cpp
)

target_include_directories(draxul-runtime-support PUBLIC
    include
)

target_link_libraries(draxul-runtime-support PUBLIC
    draxul-config
    draxul-font
    draxul-grid
    draxul-nvim
    draxul-renderer
    draxul-window
)

# pane_print composes the A4 PDF with CoreGraphics on macOS.
if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(draxul-runtime-support PRIVATE ${COREGRAPHICS_FRAMEWORK})
endif()
