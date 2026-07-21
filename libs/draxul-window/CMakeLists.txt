add_library(draxul-window
    src/sdl_window.cpp
    src/sdl_event_translator.cpp
    src/sdl_clipboard.cpp
    src/sdl_file_dialog.cpp
    $<$<PLATFORM_ID:Darwin>:src/sdl_window_macos.mm>
    $<$<PLATFORM_ID:Windows>:src/sdl_window_win32.cpp>
)
target_include_directories(draxul-window PUBLIC include)
target_link_libraries(draxul-window
    PUBLIC draxul-types SDL3::SDL3
    PRIVATE draxul-performance
)

if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(draxul-window PRIVATE ${COREGRAPHICS_FRAMEWORK} ${COCOA_FRAMEWORK})
elseif(WIN32)
    target_link_libraries(draxul-window PRIVATE Shcore Dwmapi)
endif()
