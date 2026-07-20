cmake_minimum_required(VERSION 3.25)

# draxul-satview-scene — the backend-neutral scene contract both GPU backends
# consume: vertex/uniform/instance POD records, the celestial-geometry builders
# (labels, landscape, star/constellation catalogs), and the SatViewScenePass
# snapshot container (an IRenderPass whose backend State/record() are provided
# by draxul-satview-renderer via a forward-declared pImpl). No Metal or Vulkan
# type ever appears in these public headers.

add_library(draxul-satview-scene STATIC
    src/satview_constellation_catalog.cpp
    src/satview_label_layout.cpp
    src/satview_landscape.cpp
    src/satview_star_catalog.cpp
)

target_include_directories(draxul-satview-scene
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-satview-scene PUBLIC
    draxul-satview-core
    draxul-renderer
)
