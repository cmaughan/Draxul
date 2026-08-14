set(_megacity_root "${CMAKE_CURRENT_LIST_DIR}/..")
file(GLOB _megacity_test_sources CONFIGURE_DEPENDS
    "${_megacity_root}/tests/*_tests.cpp")

draxul_add_test_target(
    draxul-test-megacity megacity 2 ${_megacity_test_sources})
target_link_libraries(draxul-test-megacity PRIVATE
    draxul-geometry
    draxul-megacity-test-internals
    draxul-codeviz-renderer-test-internals
    draxul-host
    draxul-renderer)
target_compile_definitions(draxul-test-megacity PRIVATE DRAXUL_ENABLE_MEGACITY)

add_dependencies(draxul-test-app draxul-megacity-plugin)
target_compile_definitions(draxul-test-app PRIVATE
    DRAXUL_MEGACITY_PLUGIN_PATH="$<TARGET_FILE:draxul-megacity-plugin>")
