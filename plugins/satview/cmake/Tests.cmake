set(_satview_root "${CMAKE_CURRENT_LIST_DIR}/..")
file(GLOB _satview_test_sources CONFIGURE_DEPENDS
    "${_satview_root}/tests/satview_*_tests.cpp")

draxul_add_test_target(
    draxul-test-satview satview 2 ${_satview_test_sources})
target_link_libraries(draxul-test-satview PRIVATE
    draxul-satview-runtime-test-internals
    draxul-satview-renderer-test-internals
    Draxul::PluginSupport::Config
    SDL3::SDL3
    draxul-host
    draxul-renderer)
target_compile_definitions(draxul-test-satview PRIVATE
    DRAXUL_ENABLE_SATVIEW
    "DRAXUL_SATVIEW_TEST_ASSET_ROOT=\"${_satview_root}/assets\"")

add_test(
    NAME draxul-satview-catalog-py-tests
    COMMAND ${Python3_EXECUTABLE} -m unittest satview_catalog_py_tests)
set_tests_properties(draxul-satview-catalog-py-tests PROPERTIES
    LABELS "unit;satview;python"
    TIMEOUT 120
    WORKING_DIRECTORY "${_satview_root}/tests")
