# SatView module — optional, isolated 3D satellite overview host.
#
# Added from the top-level CMakeLists.txt only when DRAXUL_ENABLE_SATVIEW is ON.
#
# Subdirectories are listed in dependency order (kanban 26): each library only
# links downward. draxul-satview-core is the pure-compute foundation; the
# remaining scene/services/renderer/host sources are being peeled out of
# draxul-satview into their own targets one boundary at a time.

add_subdirectory(draxul-satview-core)
add_subdirectory(draxul-satview-scene)
add_subdirectory(draxul-satview)
