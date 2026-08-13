# MegaCity/BioView product implementation. Only the sibling dynamic module
# links these libraries in production; Draxul core does not.

add_subdirectory(draxul-geometry)
add_subdirectory(draxul-codeviz-scene)
add_subdirectory(draxul-codeviz-renderer)
add_subdirectory(draxul-codeviz-host)
add_subdirectory(draxul-treesitter)
add_subdirectory(draxul-code-semantics)
add_subdirectory(draxul-megacity)
