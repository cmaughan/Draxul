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

add_executable(draxul-tests ${DRAXUL_TEST_SOURCES})

target_include_directories(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
if(DRAXUL_ENABLE_MEGACITY)
    target_include_directories(draxul-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/modules/megacity/draxul-megacity/src
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
target_compile_definitions(draxul-tests PRIVATE
    DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>"
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    DRAXUL_ENABLE_RENDER_TESTS
)


# Workaround: Apple Clang 16+ libc++ uses __int128 for chrono::steady_clock
# duration, which triggers ambiguous operator<< in Catch2 v3.7.x StringMaker.
# Exclude affected test file on Apple until Catch2 ships a fix.
if(APPLE)
    get_target_property(_test_sources draxul-tests SOURCES)
    list(FILTER _test_sources EXCLUDE REGEX "session_attach_tests\\.cpp$")
    set_target_properties(draxul-tests PROPERTIES SOURCES "${_test_sources}")
endif()
if(DRAXUL_ENABLE_MEGACITY)
    target_compile_definitions(draxul-tests PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()

# draxul-tests loads draxul-rpc-fake at runtime via DRAXUL_RPC_FAKE_PATH
# (4 test suites: nvim_crash, rpc_backpressure, rpc_integration, shutdown_race).
# Make the dependency explicit so building the draxul-tests target also builds
# its helper binary — otherwise those 24 tests fail with "No such file or
# directory" until someone manually builds draxul-rpc-fake.
add_dependencies(draxul-tests draxul-rpc-fake)

add_test(NAME draxul-tests COMMAND $<TARGET_FILE:draxul-tests>)

draxul_apply_sanitizers(draxul-tests)
draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-tests)
draxul_apply_coverage(draxul-rpc-fake)
draxul_apply_msvc_parallel_pdb_fix(draxul-tests)
draxul_apply_msvc_parallel_pdb_fix(draxul-rpc-fake)
