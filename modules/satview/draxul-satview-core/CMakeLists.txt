cmake_minimum_required(VERSION 3.25)

# draxul-satview-core — pure-compute foundation for SatView: catalogs,
# coordinates/frames, filters, ephemerides, propagation, and the CelesTrak
# SGP4 reference. No GPU, network, UI, or ImGui dependencies. Every backend and
# service target above sits on top of this one.

# CelesTrak SGP4 reference implementation, compiled with warnings suppressed.
set(SATVIEW_SGP4_SOURCE_DIR "${celestrak_sgp4_SOURCE_DIR}/cpp/SGP4/SGP4")
if(NOT EXISTS "${SATVIEW_SGP4_SOURCE_DIR}/SGP4.cpp")
    message(FATAL_ERROR "CelesTrak SGP4 source was not found at ${SATVIEW_SGP4_SOURCE_DIR}")
endif()

add_library(celestrak-sgp4 STATIC src/vallado_sgp4.cpp)
target_include_directories(celestrak-sgp4 SYSTEM PUBLIC "${SATVIEW_SGP4_SOURCE_DIR}")
if(MSVC)
    target_compile_options(celestrak-sgp4 PRIVATE /w)
else()
    target_compile_options(celestrak-sgp4 PRIVATE -w)
endif()

add_library(draxul-satview-core STATIC
    src/satview_catalog.cpp
    src/satview_catalog_container.cpp
    src/satview_config.cpp
    src/satview_constellation_boundary_catalog.cpp
    src/satview_filter.cpp
    src/satview_geodetic.cpp
    src/satview_ground_view.cpp
    src/satview_lunar_frame.cpp
    src/satview_lunar_surface_catalog.cpp
    src/satview_map_projection.cpp
    src/satview_moon_ephemeris.cpp
    src/satview_propagation.cpp
    src/satview_sky_projection.cpp
    src/satview_solar_system.cpp
    src/satview_sun_ephemeris.cpp
    src/satview_surface_catalog.cpp
    src/satview_texture_assets.cpp
)

target_include_directories(draxul-satview-core
    PUBLIC include
    PRIVATE src ${stb_SOURCE_DIR}
)

target_compile_definitions(draxul-satview-core PRIVATE
    "DRAXUL_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

target_link_libraries(draxul-satview-core PUBLIC
    draxul-types
)
target_link_libraries(draxul-satview-core PRIVATE
    celestrak-sgp4
    draxul-performance
)
