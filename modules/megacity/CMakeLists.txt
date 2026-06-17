# Megacity module — optional, isolated from the terminal product.
#
# This module is added from the top-level CMakeLists.txt only when
# DRAXUL_ENABLE_MEGACITY is ON. The terminal product (draxul-app and the
# draxul executable) has zero source-level dependency on anything in this
# directory; megacity self-registers with the host provider registry from
# main.cpp under #ifdef DRAXUL_ENABLE_MEGACITY.

add_subdirectory(draxul-geometry)
add_subdirectory(draxul-treesitter)
add_subdirectory(draxul-citymodel)
add_subdirectory(draxul-citydb)
add_subdirectory(draxul-megacity)
