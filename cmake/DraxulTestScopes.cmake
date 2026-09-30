# CMake owns both the build aggregate and CTest scope for each registered suite.
# Scope labels are orthogonal to unit/integration/render and subsystem labels.
set(DRAXUL_TEST_PRODUCT_SCOPES megacity satview scoreview pcbview rezonality)

function(draxul_test_scope_from_labels output)
    set(scope core)
    foreach(product IN LISTS DRAXUL_TEST_PRODUCT_SCOPES)
        if(product IN_LIST ARGN)
            if(NOT scope STREQUAL "core")
                message(FATAL_ERROR "Test has multiple product owners: ${ARGN}")
            endif()
            set(scope "${product}")
        endif()
    endforeach()
    set(${output} "${scope}" PARENT_SCOPE)
endfunction()

function(draxul_register_test_target target)
    draxul_test_scope_from_labels(scope ${ARGN})
    if(NOT TARGET draxul-tests-${scope})
        add_custom_target(draxul-tests-${scope})
    endif()
    add_dependencies(draxul-tests-${scope} ${target})
endfunction()

# Run after all normal and product script tests in the tests directory exist.
# Expensive standalone SDK and render smokes retain their separate selectors;
# product render scenarios are registered by the render manifest in the root.
function(draxul_register_test_scopes)
    get_property(tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS tests)
        get_property(labels TEST ${test} PROPERTY LABELS)
        if(test STREQUAL "draxul-external-plugin-sdk-smoke")
            continue()
        endif()
        draxul_test_scope_from_labels(scope ${labels})
        set_property(TEST ${test} APPEND PROPERTY LABELS "scope-${scope}")
    endforeach()
endfunction()
