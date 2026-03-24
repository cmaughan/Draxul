add_executable(draxul-rpc-fake
    rpc_fake_server.cpp
)
target_link_libraries(draxul-rpc-fake PRIVATE draxul-nvim)

# Auto-discover test sources: test_main.cpp + all *_tests.cpp files.
# New test files matching this pattern are picked up automatically.
file(GLOB DRAXUL_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp
)

add_executable(draxul-tests ${DRAXUL_TEST_SOURCES})

target_include_directories(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)
if(DRAXUL_ENABLE_MEGACITY)
    target_include_directories(draxul-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/draxul-megacity/src
    )
endif()
target_link_libraries(draxul-tests PRIVATE
    draxul-app
    Catch2::Catch2
)
target_compile_definitions(draxul-tests PRIVATE
    DRAXUL_RPC_FAKE_PATH="$<TARGET_FILE:draxul-rpc-fake>"
    DRAXUL_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    DRAXUL_ENABLE_RENDER_TESTS
)
if(DRAXUL_ENABLE_MEGACITY)
    target_compile_definitions(draxul-tests PRIVATE DRAXUL_ENABLE_MEGACITY)
endif()

add_test(NAME draxul-tests COMMAND $<TARGET_FILE:draxul-tests>)

draxul_apply_sanitizers(draxul-tests)
draxul_apply_sanitizers(draxul-rpc-fake)
draxul_apply_coverage(draxul-tests)
draxul_apply_coverage(draxul-rpc-fake)
