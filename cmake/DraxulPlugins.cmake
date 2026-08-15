include_guard(GLOBAL)

include(CMakeParseArguments)

# Public integration surface for native plugins mounted beneath plugins/. Product
# CMake files register their module, runtime payload, and product-owned targets;
# the root build stages the result without knowing any product-specific details.

function(_draxul_plugin_property_key output plugin_id)
    string(MAKE_C_IDENTIFIER "${plugin_id}" _key)
    string(TOLOWER "${_key}" _key)
    set(${output} "${_key}" PARENT_SCOPE)
endfunction()

function(_draxul_split_plugin_mapping mapping source_output destination_output)
    string(FIND "${mapping}" "=>" _separator)
    if(_separator EQUAL -1)
        message(FATAL_ERROR
            "Plugin payload mapping '${mapping}' must use source=>destination")
    endif()

    string(SUBSTRING "${mapping}" 0 ${_separator} _source)
    math(EXPR _destination_start "${_separator} + 2")
    string(SUBSTRING "${mapping}" ${_destination_start} -1 _destination)
    if(_source STREQUAL "" OR _destination STREQUAL "" OR IS_ABSOLUTE "${_destination}")
        message(FATAL_ERROR
            "Plugin payload mapping '${mapping}' must have a source and a relative destination")
    endif()

    set(${source_output} "${_source}" PARENT_SCOPE)
    set(${destination_output} "${_destination}" PARENT_SCOPE)
endfunction()

function(draxul_allow_plugin_support_targets)
    get_property(_allowed GLOBAL PROPERTY DRAXUL_PLUGIN_SUPPORT_TARGETS)
    list(APPEND _allowed ${ARGN})
    list(REMOVE_DUPLICATES _allowed)
    set_property(GLOBAL PROPERTY DRAXUL_PLUGIN_SUPPORT_TARGETS "${_allowed}")
endfunction()

function(draxul_register_bundled_plugin)
    set(_single_value ID TARGET MANIFEST FEATURE TEST_CMAKE DEPENDENCY_MODE)
    set(_multi_value
        COPY_FILES COPY_DIRECTORIES DEPENDS PRODUCT_TARGETS TEST_TARGETS
        TEST_SOURCES)
    cmake_parse_arguments(PLUGIN "" "${_single_value}" "${_multi_value}" ${ARGN})

    foreach(_required ID TARGET MANIFEST)
        if(NOT PLUGIN_${_required})
            message(FATAL_ERROR
                "draxul_register_bundled_plugin requires ${_required}")
        endif()
    endforeach()
    if(PLUGIN_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments to draxul_register_bundled_plugin: ${PLUGIN_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT PLUGIN_DEPENDENCY_MODE)
        set(PLUGIN_DEPENDENCY_MODE STRICT)
    endif()
    if(NOT PLUGIN_DEPENDENCY_MODE MATCHES "^(STRICT|MIGRATING)$")
        message(FATAL_ERROR
            "Plugin '${PLUGIN_ID}' DEPENDENCY_MODE must be STRICT or MIGRATING")
    endif()
    if(NOT PLUGIN_ID MATCHES "^[a-z0-9._-]+$")
        message(FATAL_ERROR
            "Invalid plugin ID '${PLUGIN_ID}'; use lowercase letters, digits, '.', '_' and '-'")
    endif()
    string(LENGTH "${PLUGIN_ID}" _id_length)
    if(_id_length GREATER 128)
        message(FATAL_ERROR "Plugin ID '${PLUGIN_ID}' exceeds 128 characters")
    endif()
    if(NOT TARGET ${PLUGIN_TARGET})
        message(FATAL_ERROR
            "Plugin '${PLUGIN_ID}' names missing module target '${PLUGIN_TARGET}'")
    endif()
    if(NOT EXISTS "${PLUGIN_MANIFEST}")
        message(FATAL_ERROR
            "Plugin '${PLUGIN_ID}' names missing manifest '${PLUGIN_MANIFEST}'")
    endif()

    # CMake gives MODULE libraries a .so suffix on macOS, but Draxul plugin
    # manifests intentionally use the platform-native .dylib filename.
    if(APPLE)
        set_target_properties(${PLUGIN_TARGET} PROPERTIES SUFFIX ".dylib")
        # Export only the C ABI entry point. Without this, every inline C++
        # function the plugin shares with the host (Dear ImGui's header
        # inlines especially) is emitted as a coalescible weak external, and
        # dyld unifies those across images at load — so the plugin's
        # ImGui::NewFrame would call the HOST executable's GetDefaultFont,
        # which reads the host's GImGui and mixes two ImGui context
        # universes. Hiding the plugin's exports keeps its weak definitions
        # image-local while -undefined dynamic_lookup still binds SDL_* to
        # the host at dlopen.
        target_link_options(${PLUGIN_TARGET} PRIVATE
            "LINKER:-exported_symbol,_draxul_plugin_query_v2")
    endif()
    if(PLUGIN_TEST_CMAKE AND NOT EXISTS "${PLUGIN_TEST_CMAKE}")
        message(FATAL_ERROR
            "Plugin '${PLUGIN_ID}' names missing test wiring '${PLUGIN_TEST_CMAKE}'")
    endif()

    get_property(_plugin_ids GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS)
    if(PLUGIN_ID IN_LIST _plugin_ids)
        message(FATAL_ERROR "Bundled plugin ID '${PLUGIN_ID}' was registered twice")
    endif()

    foreach(_product_target IN LISTS PLUGIN_PRODUCT_TARGETS)
        if(NOT TARGET ${_product_target})
            message(FATAL_ERROR
                "Plugin '${PLUGIN_ID}' names missing product target '${_product_target}'")
        endif()
    endforeach()
    foreach(_test_target IN LISTS PLUGIN_TEST_TARGETS)
        if(NOT TARGET ${_test_target})
            message(FATAL_ERROR
                "Plugin '${PLUGIN_ID}' names missing test target '${_test_target}'")
        endif()
    endforeach()

    _draxul_plugin_property_key(_key "${PLUGIN_ID}")
    list(APPEND _plugin_ids "${PLUGIN_ID}")
    set_property(GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS "${_plugin_ids}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_TARGET" "${PLUGIN_TARGET}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_MANIFEST" "${PLUGIN_MANIFEST}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_COPY_FILES" "${PLUGIN_COPY_FILES}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_COPY_DIRECTORIES" "${PLUGIN_COPY_DIRECTORIES}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_DEPENDS" "${PLUGIN_DEPENDS}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_FEATURE" "${PLUGIN_FEATURE}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_TEST_CMAKE"
        "${PLUGIN_TEST_CMAKE}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_DEPENDENCY_MODE"
        "${PLUGIN_DEPENDENCY_MODE}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_TEST_TARGETS"
        "${PLUGIN_TEST_TARGETS}")
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_TEST_SOURCES"
        "${PLUGIN_TEST_SOURCES}")
    set(_all_product_targets ${PLUGIN_TARGET} ${PLUGIN_PRODUCT_TARGETS})
    list(REMOVE_ITEM _all_product_targets "")
    list(REMOVE_DUPLICATES _all_product_targets)
    set_property(GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_PRODUCT_TARGETS"
        "${_all_product_targets}")
endfunction()

function(draxul_get_registered_plugin_test_sources output)
    set(_sources)
    get_property(_plugin_ids GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS)
    foreach(_plugin_id IN LISTS _plugin_ids)
        _draxul_plugin_property_key(_key "${_plugin_id}")
        get_property(_plugin_sources GLOBAL
            PROPERTY "DRAXUL_PLUGIN_${_key}_TEST_SOURCES")
        list(APPEND _sources ${_plugin_sources})
    endforeach()
    set(${output} "${_sources}" PARENT_SCOPE)
endfunction()

function(_draxul_unwrap_link_item output item)
    set(_item "${item}")
    if(_item MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
        set(_item "${CMAKE_MATCH_1}")
    elseif(_item MATCHES "^\\$<BUILD_INTERFACE:([^>]+)>$")
        set(_item "${CMAKE_MATCH_1}")
    endif()
    set(${output} "${_item}" PARENT_SCOPE)
endfunction()

function(_draxul_resolve_alias output target)
    get_target_property(_aliased_target ${target} ALIASED_TARGET)
    if(_aliased_target)
        set(${output} "${_aliased_target}" PARENT_SCOPE)
    else()
        set(${output} "${target}" PARENT_SCOPE)
    endif()
endfunction()

function(draxul_check_plugin_support_dependencies)
    get_property(_allowed GLOBAL PROPERTY DRAXUL_PLUGIN_SUPPORT_TARGETS)
    set(_support_targets)
    foreach(_allowed_target IN LISTS _allowed)
        if(NOT TARGET ${_allowed_target})
            message(FATAL_ERROR
                "Plugin support allowlist names missing target '${_allowed_target}'")
        endif()
        _draxul_resolve_alias(_support_target "${_allowed_target}")
        list(APPEND _support_targets "${_support_target}")
    endforeach()
    list(REMOVE_DUPLICATES _support_targets)

    foreach(_support_target IN LISTS _support_targets)
        foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links ${_support_target} ${_property})
            if(NOT _links)
                continue()
            endif()
            foreach(_link IN LISTS _links)
                _draxul_unwrap_link_item(_link_target "${_link}")
                if(NOT TARGET ${_link_target})
                    continue()
                endif()
                _draxul_resolve_alias(_resolved_link "${_link_target}")
                if(_resolved_link IN_LIST _support_targets)
                    continue()
                endif()
                if(_resolved_link MATCHES "^draxul-"
                    OR _link_target MATCHES "^Draxul::")
                    message(FATAL_ERROR
                        "Plugin support target '${_support_target}' reaches non-support "
                        "Draxul target '${_link_target}'. Split a smaller leaf target "
                        "instead of widening the plugin surface.")
                endif()
            endforeach()
        endforeach()
    endforeach()
endfunction()

function(draxul_check_registered_plugin_dependencies)
    get_property(_plugin_ids GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS)
    get_property(_allowed GLOBAL PROPERTY DRAXUL_PLUGIN_SUPPORT_TARGETS)

    foreach(_plugin_id IN LISTS _plugin_ids)
        _draxul_plugin_property_key(_key "${_plugin_id}")
        get_property(_dependency_mode GLOBAL
            PROPERTY "DRAXUL_PLUGIN_${_key}_DEPENDENCY_MODE")
        if(_dependency_mode STREQUAL "MIGRATING")
            message(STATUS
                "Plugin '${_plugin_id}' is registered with migrating C++ support dependencies")
            continue()
        endif()
        get_property(_product_targets GLOBAL
            PROPERTY "DRAXUL_PLUGIN_${_key}_PRODUCT_TARGETS")

        foreach(_product_target IN LISTS _product_targets)
            if(_product_target STREQUAL "")
                continue()
            endif()
            foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
                get_target_property(_links ${_product_target} ${_property})
                if(NOT _links)
                    continue()
                endif()

                foreach(_link IN LISTS _links)
                    _draxul_unwrap_link_item(_link_target "${_link}")
                    if(NOT TARGET ${_link_target})
                        continue()
                    endif()
                    if(_link_target IN_LIST _product_targets OR _link_target IN_LIST _allowed)
                        continue()
                    endif()
                    if(_link_target MATCHES "^draxul-" OR _link_target MATCHES "^Draxul::")
                        message(FATAL_ERROR
                            "Plugin '${_plugin_id}' target '${_product_target}' links "
                            "non-public Draxul target '${_link_target}'. Add a narrow generic "
                            "support target to the allowlist; do not reach into core or another product.")
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endforeach()
endfunction()

function(draxul_configure_registered_plugin_tests)
    get_property(_plugin_ids GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS)
    foreach(_plugin_id IN LISTS _plugin_ids)
        _draxul_plugin_property_key(_key "${_plugin_id}")
        get_property(_test_cmake GLOBAL
            PROPERTY "DRAXUL_PLUGIN_${_key}_TEST_CMAKE")
        if(_test_cmake)
            include("${_test_cmake}")
        endif()
    endforeach()
endfunction()

function(draxul_stage_registered_plugins app_target)
    if(NOT TARGET ${app_target})
        message(FATAL_ERROR
            "draxul_stage_registered_plugins names missing app target '${app_target}'")
    endif()

    get_property(_plugin_ids GLOBAL PROPERTY DRAXUL_REGISTERED_PLUGIN_IDS)
    foreach(_plugin_id IN LISTS _plugin_ids)
        _draxul_plugin_property_key(_key "${_plugin_id}")
        get_property(_target GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_TARGET")
        get_property(_manifest GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_MANIFEST")
        get_property(_copy_files GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_COPY_FILES")
        get_property(_copy_directories GLOBAL
            PROPERTY "DRAXUL_PLUGIN_${_key}_COPY_DIRECTORIES")
        get_property(_depends GLOBAL PROPERTY "DRAXUL_PLUGIN_${_key}_DEPENDS")

        if(APPLE)
            set(_stage_directory
                "$<TARGET_BUNDLE_DIR:${app_target}>/Contents/PlugIns/${_plugin_id}")
        else()
            set(_stage_directory
                "$<TARGET_FILE_DIR:${app_target}>/plugins/${_plugin_id}")
        endif()

        set(_commands
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${_stage_directory}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_stage_directory}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_manifest}" "${_stage_directory}/plugin.toml"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${_target}>"
                "${_stage_directory}/$<TARGET_FILE_NAME:${_target}>")

        set(_payload_dependencies)
        foreach(_mapping IN LISTS _copy_files)
            _draxul_split_plugin_mapping("${_mapping}" _source _destination)
            get_filename_component(_destination_directory "${_destination}" DIRECTORY)
            if(_destination_directory AND NOT _destination_directory STREQUAL ".")
                list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E make_directory
                    "${_stage_directory}/${_destination_directory}")
            endif()
            list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_source}" "${_stage_directory}/${_destination}")
            list(APPEND _payload_dependencies "${_source}")
        endforeach()

        foreach(_mapping IN LISTS _copy_directories)
            _draxul_split_plugin_mapping("${_mapping}" _source _destination)
            list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E make_directory
                "${_stage_directory}/${_destination}")
            list(APPEND _commands COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${_source}" "${_stage_directory}/${_destination}")
            list(APPEND _payload_dependencies "${_source}")
        endforeach()

        set(_stage_target "draxul-stage-plugin-${_key}")
        add_custom_target(${_stage_target}
            ${_commands}
            DEPENDS ${_target} "${_manifest}" ${_depends} ${_payload_dependencies}
            VERBATIM)
        add_dependencies(${app_target} ${_stage_target})
    endforeach()
endfunction()
