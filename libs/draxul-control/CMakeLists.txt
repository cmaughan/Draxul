add_library(draxul-control STATIC
    src/control_plane.cpp
)

target_include_directories(draxul-control PUBLIC include)

target_link_libraries(draxul-control PUBLIC
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(draxul-control PRIVATE advapi32)
endif()
