add_library(draxul-host-identity INTERFACE)
target_include_directories(draxul-host-identity INTERFACE include)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
