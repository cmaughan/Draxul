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

function(draxul_check_dependency_boundaries)
    draxul_check_direct_link(draxul-types draxul-performance)
    draxul_check_direct_link(draxul-bmp draxul-types)
    draxul_check_direct_link(draxul-bmp draxul-performance)
    draxul_check_direct_link(draxul-terminal-core draxul-grid)
    draxul_check_direct_link(draxul-terminal-core draxul-types)
    draxul_check_direct_link(draxul-terminal-core draxul-performance)
    draxul_check_direct_link(draxul-terminal-process draxul-agent)
    draxul_check_direct_link(draxul-session-model draxul-agent)
    draxul_check_direct_link(draxul-session-model draxul-host-identity)
    draxul_check_direct_link(draxul-protocol draxul-agent)
    draxul_check_direct_link(draxul-runtime-support draxul-host-identity)
    draxul_check_direct_link(draxul-host draxul-host-identity)
    draxul_check_direct_link(draxul-host draxul-client)
    draxul_check_direct_link(draxul-host draxul-nvim)
    draxul_reject_direct_links(draxul-host draxul-terminal-process)
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
        draxul-nvim draxul-gui draxul-ui SDL3::SDL3)
    draxul_reject_direct_links(draxul-client
        draxul-window draxul-renderer draxul-runtime-support draxul-host
        draxul-nvim draxul-gui draxul-ui SDL3::SDL3)
    draxul_reject_direct_links(draxul-server
        draxul-window draxul-renderer draxul-runtime-support draxul-host
        draxul-nvim draxul-gui draxul-ui SDL3::SDL3)

    set(_product_target_pattern
        "draxul-(markdown|kanban|megacity|codeviz|satview|scoreview|score-|notation)")
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
        "draxul-(megacity|codeviz|satview|scoreview|score-|notation|geometry|treesitter|code-semantics)")
    foreach(_core_target
        draxul-performance
        draxul-host-identity
        draxul-types
        draxul-plugin
        draxul-bmp
        draxul-http
        draxul-window
        draxul-renderer
        draxul-ui
        draxul-font
        draxul-grid
        draxul-terminal-core
        draxul-nvim
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
        draxul-app-support
        draxul-host
        draxul-nanovg
        draxul-markdown
        draxul-markdown-host
        draxul-kanban
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
        "^[ \t]*#[ \t]*include[ \t]*[<\"][^>\"]*(megacity|codeviz|satview|scoreview|bioview|notation|code_semantics|treesitter)")
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
