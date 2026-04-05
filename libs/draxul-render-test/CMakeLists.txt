add_library(draxul-render-test
    src/render_test.cpp
    src/nanovg_demo_host.cpp
)

target_include_directories(draxul-render-test PUBLIC
    include
)

target_compile_definitions(draxul-render-test PRIVATE
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

if(DRAXUL_ENABLE_RENDER_TESTS)
    target_compile_definitions(draxul-render-test PUBLIC DRAXUL_ENABLE_RENDER_TESTS)
endif()

target_link_libraries(draxul-render-test PUBLIC
    draxul-config
    draxul-font
    draxul-host
    draxul-nanovg
    draxul-renderer
)
