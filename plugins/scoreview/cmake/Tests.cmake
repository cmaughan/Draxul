set(_scoreview_root "${CMAKE_CURRENT_LIST_DIR}/..")
include(FetchContent)
FetchContent_GetProperties(verovio)
FetchContent_GetProperties(ydp_grand_piano)
if(NOT verovio_SOURCE_DIR OR NOT ydp_grand_piano_SOURCE_DIR)
    message(FATAL_ERROR
        "ScoreView test resources were not populated by the product dependency graph")
endif()
file(GLOB _scoreview_test_sources CONFIGURE_DEPENDS
    "${_scoreview_root}/tests/notation_*_tests.cpp"
    "${_scoreview_root}/tests/scoreview_*_tests.cpp")
set(_scoreview_runtime_test_sources
    "${_scoreview_root}/tests/scoreview_composer_tests.cpp"
    "${_scoreview_root}/tests/scoreview_host_orchestration_tests.cpp"
    "${_scoreview_root}/tests/scoreview_host_rebuild_tests.cpp"
    "${_scoreview_root}/tests/scoreview_microphone_tests.cpp"
    "${_scoreview_root}/tests/scoreview_overlay_tests.cpp"
    "${_scoreview_root}/tests/scoreview_worker_stress_tests.cpp")
list(REMOVE_ITEM _scoreview_test_sources ${_scoreview_runtime_test_sources})

draxul_add_test_target(
    draxul-test-scoreview scoreview 2 ${_scoreview_test_sources})
target_link_libraries(draxul-test-scoreview PRIVATE
    draxul-score-runtime-support draxul-notation draxul-scoreview)

draxul_add_test_target(
    draxul-test-scoreview-runtime "scoreview;scoreview-runtime" 1
    ${_scoreview_runtime_test_sources})
target_link_libraries(draxul-test-scoreview-runtime PRIVATE
    draxul-scoreview-runtime-test-internals)

foreach(_target draxul-test-scoreview draxul-test-scoreview-runtime)
    target_include_directories(${_target} PRIVATE "${_scoreview_root}/tests")
    target_compile_definitions(${_target} PRIVATE
        DRAXUL_ENABLE_SCOREVIEW
        DRAXUL_VEROVIO_DATA_DIR="${verovio_SOURCE_DIR}/data"
        DRAXUL_SOUNDFONT_DIR="${ydp_grand_piano_SOURCE_DIR}")
    if(WIN32)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:verovio> $<TARGET_FILE_DIR:${_target}>)
    endif()
endforeach()

add_dependencies(draxul-test-app draxul-scoreview-plugin)
target_compile_definitions(draxul-test-app PRIVATE
    DRAXUL_SCOREVIEW_PLUGIN_PATH="$<TARGET_FILE:draxul-scoreview-plugin>")
if(WIN32)
    add_custom_command(TARGET draxul-test-app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:verovio> $<TARGET_FILE_DIR:draxul-test-app>)
endif()

# Cold copied-tree extraction remains opt-in because rebuilding Verovio is
# intentionally too expensive for the ordinary smoke loop.
set(_scoreview_extraction_command
    ${Python3_EXECUTABLE}
    "${_scoreview_root}/tests/external_product_plugin_smoke.py"
    --cmake ${CMAKE_COMMAND}
    --source-root ${CMAKE_SOURCE_DIR}
    --build-root ${CMAKE_BINARY_DIR}
    --draxul $<TARGET_FILE:draxul>
    --config $<CONFIG>
    --generator ${CMAKE_GENERATOR}
    --platform=${CMAKE_GENERATOR_PLATFORM}
    --toolset=${CMAKE_GENERATOR_TOOLSET})
if(DRAXUL_ENABLE_RENDER_TESTS)
    list(APPEND _scoreview_extraction_command --render)
endif()
add_custom_target(draxul-scoreview-extraction-smoke
    COMMAND ${_scoreview_extraction_command}
    DEPENDS draxul
    USES_TERMINAL
    VERBATIM)
