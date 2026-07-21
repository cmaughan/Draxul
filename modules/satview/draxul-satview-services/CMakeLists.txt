cmake_minimum_required(VERSION 3.25)

# draxul-satview-services — catalog refresh and cloud-texture services that sit
# above the draxul-http transport. This is the only SatView boundary that talks
# to the network/cache; core stays pure-compute below it. Links
# draxul-satview-core (catalog/texture types) and draxul-http PUBLIC.

add_library(draxul-satview-services STATIC
    src/satview_catalog_service.cpp
    src/satview_cloud_service.cpp
)

target_include_directories(draxul-satview-services
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-satview-services PUBLIC
    draxul-satview-core
    draxul-http
)
target_link_libraries(draxul-satview-services PRIVATE draxul-performance)
