add_library(draxul-runtime-support
    src/cursor_blinker.cpp
    src/grid_rendering_pipeline.cpp
    src/pane_print.cpp
    src/session_attach.cpp
    src/session_attach_protocol.cpp
    src/session_attach_transport.cpp
    src/system_resource_monitor.cpp
    src/ui_request_worker.cpp
)

if(WIN32)
    target_sources(draxul-runtime-support PRIVATE src/session_attach_win.cpp)
else()
    target_sources(draxul-runtime-support PRIVATE src/session_attach_posix.cpp)
endif()

target_include_directories(draxul-runtime-support PUBLIC
    include
)

target_link_libraries(draxul-runtime-support PUBLIC
    draxul-config
    draxul-font
    draxul-grid
    draxul-host-identity
    draxul-nvim
    draxul-renderer
    draxul-window
)
target_link_libraries(draxul-runtime-support PRIVATE draxul-performance)

# pane_print composes the A4 PDF with CoreGraphics on macOS and presents
# the native print dialog through PDFKit/AppKit.
if(APPLE)
    target_sources(draxul-runtime-support PRIVATE src/pane_print_mac.mm)
    set_source_files_properties(src/pane_print_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(PDFKIT_FRAMEWORK PDFKit REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(draxul-runtime-support PRIVATE
        ${COREGRAPHICS_FRAMEWORK} ${PDFKIT_FRAMEWORK} ${APPKIT_FRAMEWORK})
endif()

# Session protocol/transport tests exercise the platform-neutral internal
# contract without publishing it as application API.
add_library(draxul-runtime-support-test-internals INTERFACE)
target_include_directories(draxul-runtime-support-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-runtime-support-test-internals INTERFACE
    draxul-runtime-support
)
