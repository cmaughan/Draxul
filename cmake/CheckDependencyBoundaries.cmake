function(draxul_check_direct_link target dependency)
    get_target_property(_links ${target} LINK_LIBRARIES)
    if(NOT dependency IN_LIST _links)
        message(FATAL_ERROR
            "${target} must declare a direct dependency on ${dependency}; "
            "a transitive include/link path is not an ownership boundary")
    endif()
endfunction()

function(draxul_check_dependency_boundaries)
    draxul_check_direct_link(draxul-types draxul-performance)
    draxul_check_direct_link(draxul-bmp draxul-types)
    draxul_check_direct_link(draxul-bmp draxul-performance)
    draxul_check_direct_link(draxul-runtime-support draxul-host-identity)
    draxul_check_direct_link(draxul-host draxul-host-identity)
    draxul_check_direct_link(draxul-host draxul-nvim)

    set(_product_target_pattern
        "draxul-(markdown|kanban|megacity|codeviz|satview|scoreview|score-|notation)")
    foreach(_foundation_target
        draxul-performance
        draxul-host-identity
        draxul-types
        draxul-bmp)
        foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links ${_foundation_target} ${_property})
            if(_links AND "${_links}" MATCHES "${_product_target_pattern}")
                message(FATAL_ERROR
                    "Product dependency leaked into foundation target "
                    "${_foundation_target}: ${_links}")
            endif()
        endforeach()
    endforeach()
endfunction()
