add_executable(draxul-rpc-fake
    rpc_fake_server.cpp
)
target_link_libraries(draxul-rpc-fake PRIVATE draxul-nvim)

# Auto-discover test sources: test_main.cpp + all *_tests.cpp files.
# CONFIGURE_DEPENDS: re-run CMake when new *_tests.cpp files are added
# so they are picked up without a manual reconfigure.
file(GLOB DRAXUL_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp
)

if(NOT DRAXUL_ENABLE_SATVIEW)
    list(FILTER DRAXUL_TEST_SOURCES EXCLUDE REGEX "satview_.*_tests\\.cpp$")
endif()

if(NOT DRAXUL_ENABLE_SCOREVIEW)
    list(FILTER DRAXUL_TEST_SOURCES EXCLUDE REGEX "(notation|scoreview)_.*_tests\\.cpp$")
endif()

add_executable(draxul-tests ${DRAXUL_TEST_SOURCES})

target_precompile_headers(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support/test_pch.h
)

target_include_directories(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
if(DRAXUL_ENABLE_MEGACITY)
    target_include_directories(draxul-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/megacity/draxul-megacity/src
        ${CMAKE_SOURCE_DIR}/modules/megacity/draxul-codeviz-renderer/src
    )
endif()
target_link_libraries(draxul-tests PRIVATE
    draxul-app
    draxul-markdown
    draxul-markdown-host
    draxul-kanban
    Catch2::Catch2
)
if(DRAXUL_ENABLE_MEGACITY)
    target_link_libraries(draxul-tests PRIVATE draxul-geometry draxul-megacity)
endif()
if(DRAXUL_ENABLE_SATVIEW)
    target_include_directories(draxul-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/satview/draxul-satview/src
    )
    target_link_libraries(draxul-tests PRIVATE draxul-satview)
    target_compile_definitions(draxul-tests PRIVATE DRAXUL_ENABLE_SATVIEW)
endif()
if(DRAXUL_ENABLE_SCOREVIEW)
    target_link_libraries(draxul-tests PRIVATE
        draxul-notation
        draxul-scoreview
        draxul-scoreview-host
    )
    # Host-internal components (session/audio controllers) are tested
    # directly; their headers are internal by design (kanban 21).
    target_include_directories(draxul-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/score/draxul-scoreview/src
    )
    # Layout-engine tests read Verovio's resources straight from the fetched
    # source tree; the app uses the staged <exe-dir>/verovio-data copy instead.
    target_compile_definitions(draxul-tests PRIVATE
        DRAXUL_VEROVIO_DATA_DIR="${verovio_SOURCE_DIR}/data"
        DRAXUL_SOUNDFONT_DIR="${ydp_grand_piano_SOURCE_DIR}"
    )
    if(WIN32)
        add_custom_command(TARGET draxul-tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:verovio>
                $<TARGET_FILE_DIR:draxul-tests>
        )
    endif()
endif()
target_compile_definitions(draxul-tests PRIVATE
    DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>"
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    DRAXUL_ENABLE_RENDER_TESTS
)

if(DRAXUL_ENABLE_MEGACITY)
    target_compile_definitions(draxul-tests PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()

# draxul-tests loads draxul-rpc-fake at runtime via DRAXUL_RPC_FAKE_PATH
# (4 test suites: nvim_crash, rpc_backpressure, rpc_integration, shutdown_race).
# Make the dependency explicit so building the draxul-tests target also builds
# its helper binary — otherwise those 24 tests fail with "No such file or
# directory" until someone manually builds draxul-rpc-fake.
add_dependencies(draxul-tests draxul-rpc-fake)

foreach(DRAXUL_TEST_SHARD_INDEX RANGE 0 3)
    add_test(
        NAME draxul-tests-shard-${DRAXUL_TEST_SHARD_INDEX}
        COMMAND $<TARGET_FILE:draxul-tests>
            --shard-count 4
            --shard-index ${DRAXUL_TEST_SHARD_INDEX}
    )
    set_tests_properties(draxul-tests-shard-${DRAXUL_TEST_SHARD_INDEX}
        PROPERTIES
            LABELS unit
            TIMEOUT 300
    )
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME draxul-do-py-tests
    COMMAND ${Python3_EXECUTABLE} -m unittest tests.do_py_tests
)
set_tests_properties(draxul-do-py-tests
    PROPERTIES
        LABELS unit
        TIMEOUT 120
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

if(DRAXUL_ENABLE_SATVIEW)
    # Shared catalog container writer + generator determinism (runs offline on
    # synthetic inputs). Mirrors tests/satview_catalog_container_tests.cpp.
    add_test(
        NAME draxul-satview-catalog-py-tests
        COMMAND ${Python3_EXECUTABLE} -m unittest tests.satview_catalog_py_tests
    )
    set_tests_properties(draxul-satview-catalog-py-tests
        PROPERTIES
            LABELS unit
            TIMEOUT 120
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

draxul_apply_sanitizers(draxul-tests)
draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-tests)
draxul_apply_coverage(draxul-rpc-fake)
draxul_apply_msvc_parallel_pdb_fix(draxul-tests)
draxul_apply_msvc_parallel_pdb_fix(draxul-rpc-fake)
