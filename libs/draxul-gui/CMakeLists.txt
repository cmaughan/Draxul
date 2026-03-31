add_library(draxul-gui STATIC
    src/tooltip.cpp
    src/palette_renderer.cpp
)

target_include_directories(draxul-gui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(draxul-gui PUBLIC
    draxul-types
    draxul-font
    draxul-renderer
)

# Optional: Add any GUI-specific defines or properties here.
set_target_properties(draxul-gui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
