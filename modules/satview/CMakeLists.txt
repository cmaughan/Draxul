# SatView module — optional, isolated 3D satellite overview host.
#
# Added from the top-level CMakeLists.txt only when DRAXUL_ENABLE_SATVIEW is ON.
#
# Subdirectories are listed in dependency order (kanban 26): each library only
# links downward. core is the pure-compute foundation; scene is the
# backend-neutral contract both GPU backends consume; services owns network/
# cache catalog refresh; renderer holds the private Metal/Vulkan backends; and
# host is the ImGui/input/camera/config orchestration the executable links.

add_subdirectory(draxul-satview-core)
add_subdirectory(draxul-satview-scene)
add_subdirectory(draxul-satview-services)
add_subdirectory(draxul-satview-renderer)
add_subdirectory(draxul-satview-host)
