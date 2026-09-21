# Apply project compile/link policy once to each internal target. Internal
# directory CMake files call draxul_configure_internal_targets_in_directory()
# after declaring their targets; the final recursive audit rejects omissions.

function(draxul_configure_internal_target target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Cannot configure missing internal target '${target}'")
    endif()

    get_target_property(_aliased "${target}" ALIASED_TARGET)
    if(_aliased)
        set(target "${_aliased}")
    endif()
    get_target_property(_already_configured "${target}"
        DRAXUL_INTERNAL_POLICY_CONFIGURED)
    if(_already_configured)
        return()
    endif()

    get_target_property(_imported "${target}" IMPORTED)
    get_target_property(_type "${target}" TYPE)
    if(_imported)
        set_property(TARGET "${target}" PROPERTY
            DRAXUL_INTERNAL_POLICY_CONFIGURED "SKIPPED_IMPORTED")
        return()
    endif()
    if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
        set_property(TARGET "${target}" PROPERTY
            DRAXUL_INTERNAL_POLICY_CONFIGURED "SKIPPED_${_type}")
        return()
    endif()
    if(NOT _type MATCHES
       "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
        message(FATAL_ERROR
            "Internal target '${target}' has unsupported policy type '${_type}'")
    endif()

    draxul_apply_sanitizers("${target}")
    draxul_apply_coverage("${target}")
    draxul_apply_msvc_parallel_pdb_fix("${target}")
    set_property(TARGET "${target}" PROPERTY
        DRAXUL_INTERNAL_POLICY_CONFIGURED TRUE)
endfunction()

function(draxul_configure_internal_targets_in_directory directory)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        draxul_configure_internal_target("${_target}")
    endforeach()
endfunction()

function(_draxul_collect_buildsystem_targets output directory)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        _draxul_collect_buildsystem_targets(_children "${_subdirectory}")
        list(APPEND _targets ${_children})
    endforeach()
    list(REMOVE_DUPLICATES _targets)
    set(${output} "${_targets}" PARENT_SCOPE)
endfunction()

function(draxul_audit_internal_targets source_root)
    _draxul_collect_buildsystem_targets(_targets "${source_root}")
    foreach(_target IN LISTS _targets)
        get_target_property(_source_dir "${_target}" SOURCE_DIR)
        # FetchContent and installed/imported dependencies live outside the
        # source tree. Mounted product repositories own their own policy.
        if(NOT _source_dir MATCHES "^${source_root}(/|$)"
           OR _source_dir MATCHES "^${CMAKE_BINARY_DIR}(/|$)"
           OR _source_dir MATCHES
              "^${source_root}/plugins/(megacity|satview|scoreview|pcbview|rezonality)(/|$)")
            continue()
        endif()
        get_target_property(_configured "${_target}"
            DRAXUL_INTERNAL_POLICY_CONFIGURED)
        if(NOT _configured)
            message(FATAL_ERROR
                "Internal target '${_target}' in '${_source_dir}' did not call "
                "draxul_configure_internal_target() (directly or through its "
                "directory policy call)")
        endif()
    endforeach()
endfunction()
