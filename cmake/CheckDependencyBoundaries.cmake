function(draxul_check_direct_link target dependency)
    get_target_property(_links ${target} LINK_LIBRARIES)
    if(NOT dependency IN_LIST _links)
        message(FATAL_ERROR
            "${target} must declare a direct dependency on ${dependency}; "
            "a transitive include/link path is not an ownership boundary")
    endif()
endfunction()

function(draxul_reject_direct_links target)
    get_target_property(_links ${target} LINK_LIBRARIES)
    foreach(_forbidden IN LISTS ARGN)
        if(_forbidden IN_LIST _links)
            message(FATAL_ERROR
                "${target} must not depend on ${_forbidden}; "
                "the renderer-free terminal boundary was crossed")
        endif()
    endforeach()
endfunction()

# Return target dependencies reachable through normal links and the common
# wrappers CMake adds around static-library/private and build-only edges. Other
# generator expressions are deliberately ignored: treating their payload as an
# unconditional target would make platform/configuration alternatives appear
# simultaneously active.
function(draxul_get_transitive_link_closure output root_target)
    set(_queue "${root_target}")
    set(_visited)
    set(_closure)
    while(_queue)
        list(POP_FRONT _queue _candidate)
        if(_candidate IN_LIST _visited OR NOT TARGET "${_candidate}")
            continue()
        endif()

        get_target_property(_aliased "${_candidate}" ALIASED_TARGET)
        if(_aliased)
            set(_candidate "${_aliased}")
            if(_candidate IN_LIST _visited)
                continue()
            endif()
        endif()
        list(APPEND _visited "${_candidate}")

        foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links "${_candidate}" ${_property})
            if(NOT _links OR _links STREQUAL "_links-NOTFOUND")
                continue()
            endif()
            foreach(_link IN LISTS _links)
                set(_resolved "${_link}")
                while(_resolved MATCHES
                    "^\\$<(LINK_ONLY|BUILD_INTERFACE|TARGET_NAME_IF_EXISTS):(.+)>$")
                    set(_resolved "${CMAKE_MATCH_2}")
                endwhile()
                if(_resolved MATCHES "^\\$<")
                    continue()
                endif()
                if(TARGET "${_resolved}")
                    get_target_property(_resolved_alias "${_resolved}" ALIASED_TARGET)
                    if(_resolved_alias)
                        set(_resolved "${_resolved_alias}")
                    endif()
                    if(NOT _resolved STREQUAL root_target
                       AND NOT _resolved IN_LIST _closure)
                        list(APPEND _closure "${_resolved}")
                    endif()
                    if(NOT _resolved IN_LIST _visited)
                        list(APPEND _queue "${_resolved}")
                    endif()
                endif()
            endforeach()
        endforeach()
    endwhile()
    set(${output} "${_closure}" PARENT_SCOPE)
endfunction()

function(draxul_reject_transitive_links target)
    draxul_get_transitive_link_closure(_closure "${target}")
    foreach(_forbidden IN LISTS ARGN)
        set(_forbidden_resolved "${_forbidden}")
        if(TARGET "${_forbidden}")
            get_target_property(_forbidden_alias "${_forbidden}" ALIASED_TARGET)
            if(_forbidden_alias)
                set(_forbidden_resolved "${_forbidden_alias}")
            endif()
        endif()
        if(_forbidden_resolved IN_LIST _closure)
            message(FATAL_ERROR
                "${target} must not transitively depend on ${_forbidden}; "
                "resolved closure: ${_closure}")
        endif()
    endforeach()
endfunction()

function(draxul_check_dependency_boundaries)
    draxul_check_direct_link(draxul-types draxul-performance)
    draxul_check_direct_link(draxul-bmp draxul-types)
    draxul_check_direct_link(draxul-bmp draxul-performance)
    draxul_check_direct_link(draxul-weather draxul-http)
    draxul_check_direct_link(draxul-terminal-core draxul-grid)
    draxul_check_direct_link(draxul-terminal-core draxul-types)
    draxul_check_direct_link(draxul-terminal-core draxul-performance)
    draxul_check_direct_link(draxul-terminal-process draxul-agent)
    draxul_check_direct_link(draxul-session-model draxul-agent)
    draxul_check_direct_link(draxul-session-model draxul-host-identity)
    draxul_check_direct_link(draxul-session-model draxul-plugin-config-support)
    draxul_check_direct_link(draxul-protocol draxul-agent)
    draxul_check_direct_link(draxul-protocol draxul-session-model)
    draxul_check_direct_link(draxul-runtime-support draxul-host-identity)
    draxul_check_direct_link(draxul-runtime-support draxul-nvim-protocol)
    draxul_check_direct_link(draxul-nvim-protocol draxul-types)
    draxul_check_direct_link(draxul-nvim-transport draxul-nvim-protocol)
    draxul_check_direct_link(draxul-host-api draxul-agent)
    draxul_check_direct_link(draxul-host-api draxul-host-identity)
    draxul_check_direct_link(draxul-host-api draxul-types)
    draxul_check_direct_link(draxul-grid-host draxul-host-api)
    draxul_check_direct_link(draxul-terminal-host draxul-client)
    draxul_check_direct_link(draxul-terminal-host draxul-terminal-core)
    draxul_check_direct_link(draxul-nvim-host draxul-nvim-protocol)
    draxul_check_direct_link(draxul-nvim-host draxul-nvim-transport)
    draxul_check_direct_link(draxul-plugin-host draxul-plugin)
    draxul_reject_direct_links(draxul-host-api
        draxul-client draxul-grid draxul-renderer
        draxul-runtime-support draxul-terminal-core draxul-window)
    draxul_reject_direct_links(draxul-grid-host
        draxul-client draxul-terminal-core draxul-terminal-process)
    draxul_reject_direct_links(draxul-terminal-host
        draxul-nvim-protocol draxul-nvim-transport
        draxul-terminal-process)
    draxul_reject_direct_links(draxul-nvim-protocol
        draxul-host draxul-nvim-transport draxul-renderer
        draxul-runtime-support draxul-window)
    draxul_check_direct_link(draxul-client draxul-control)
    draxul_check_direct_link(draxul-client draxul-protocol)
    draxul_check_direct_link(draxul-server draxul-control)
    draxul_check_direct_link(draxul-server draxul-protocol)
    draxul_check_direct_link(draxul-server draxul-terminal-process)
    draxul_check_direct_link(draxul-protocol draxul-terminal-core)
    draxul_reject_direct_links(draxul-terminal-core
        draxul-host
        draxul-window
        draxul-renderer
        draxul-runtime-support
        draxul-font
        draxul-gui
        draxul-ui
        SDL3::SDL3)
    draxul_reject_direct_links(draxul-terminal-process
        draxul-host
        draxul-window
        draxul-renderer
        draxul-runtime-support
        draxul-font
        draxul-gui
        draxul-ui
        SDL3::SDL3)
    draxul_reject_direct_links(draxul-protocol
        draxul-window draxul-renderer draxul-runtime-support draxul-host
        draxul-gui draxul-ui SDL3::SDL3)
    draxul_reject_direct_links(draxul-client
        draxul-window draxul-renderer draxul-runtime-support draxul-host
        draxul-gui draxul-ui SDL3::SDL3)
    draxul_reject_direct_links(draxul-server
        draxul-window draxul-renderer draxul-runtime-support draxul-host
        draxul-gui draxul-ui SDL3::SDL3)

    foreach(_headless_target
        draxul-session-model draxul-protocol draxul-client draxul-server)
        draxul_reject_transitive_links(${_headless_target}
            draxul-config
            SDL3::SDL3
            draxul-window
            draxul-renderer
            draxul-ui
            draxul-gui
            draxul-runtime-support)
    endforeach()

    foreach(_render_contract_consumer draxul-gui draxul-ui draxul-nanovg)
        draxul_reject_transitive_links(${_render_contract_consumer}
            draxul-renderer)
    endforeach()

    draxul_reject_transitive_links(draxul-weather
        draxul-app draxul-window draxul-renderer draxul-runtime-support
        draxul-host draxul-font draxul-grid draxul-gui draxul-ui SDL3::SDL3)

    set(_product_target_pattern
        "draxul-(markdown|kanban|megacity|codeviz|satview|scoreview|pcbview|score-|notation)")
    foreach(_foundation_target
        draxul-performance
        draxul-host-identity
        draxul-types
        draxul-bmp
        draxul-terminal-core
        draxul-terminal-process
        draxul-session-model
        draxul-protocol
        draxul-client
        draxul-server)
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

function(draxul_check_core_product_isolation)
    # Kanban and Markdown are intentionally core. Everything in this expression
    # is an optional external product and must remain downstream of Draxul.
    set(_external_product_target_pattern
        "draxul-(megacity|codeviz|satview|scoreview|pcbview|score-|notation|geometry|treesitter|code-semantics)")
    foreach(_core_target
        draxul-performance
        draxul-host-identity
        draxul-types
        draxul-plugin
        draxul-bmp
        draxul-http
        draxul-weather
        draxul-window
        draxul-renderer
        draxul-ui
        draxul-font
        draxul-grid
        draxul-terminal-core
        draxul-config
        draxul-agent
        draxul-session-model
        draxul-terminal-process
        draxul-protocol
        draxul-control
        draxul-client
        draxul-server
        draxul-gui
        draxul-runtime-support
        draxul-render-test
        draxul-app-shell
        draxul-host-api
        draxul-grid-host
        draxul-terminal-host
        draxul-nvim-host
        draxul-plugin-host
        draxul-host
        draxul-nanovg
        draxul-nanovg-backend
        draxul-markdown
        draxul-markdown-host
        draxul-kanban
        draxul-kanban-host
        draxul-app
        draxul)
        if(NOT TARGET ${_core_target})
            continue()
        endif()
        foreach(_property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(_links ${_core_target} ${_property})
            if(_links AND "${_links}" MATCHES "${_external_product_target_pattern}")
                message(FATAL_ERROR
                    "External product dependency leaked into core target "
                    "${_core_target}: ${_links}")
            endif()
        endforeach()
    endforeach()
endfunction()

function(draxul_check_core_source_product_isolation source_root)
    set(_core_roots
        "${source_root}/app"
        "${source_root}/libs"
        "${source_root}/modules")
    set(_core_sources)
    foreach(_root IN LISTS _core_roots)
        file(GLOB_RECURSE _root_sources CONFIGURE_DEPENDS
            "${_root}/*.c"
            "${_root}/*.cc"
            "${_root}/*.cpp"
            "${_root}/*.h"
            "${_root}/*.hpp"
            "${_root}/*.m"
            "${_root}/*.mm")
        list(APPEND _core_sources ${_root_sources})
    endforeach()

    set(_external_product_include_pattern
        "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]*(megacity|codeviz|satview|scoreview|pcbview|bioview|notation|code_semantics|treesitter)")
    foreach(_source IN LISTS _core_sources)
        file(STRINGS "${_source}" _forbidden_includes
            REGEX "${_external_product_include_pattern}")
        if(_forbidden_includes)
            message(FATAL_ERROR
                "Core source '${_source}' includes an external product header: "
                "${_forbidden_includes}")
        endif()
    endforeach()
endfunction()
