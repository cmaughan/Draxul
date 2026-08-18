set(DRAXUL_CONTROL_SOURCES
    src/control_codec.cpp
    src/control_metadata.cpp
    src/control_plane.cpp
)

if(WIN32)
    list(APPEND DRAXUL_CONTROL_SOURCES src/control_transport_win32.cpp)
else()
    list(APPEND DRAXUL_CONTROL_SOURCES src/control_transport_posix.cpp)
endif()

add_library(draxul-control STATIC ${DRAXUL_CONTROL_SOURCES})

target_include_directories(draxul-control PUBLIC include)

target_link_libraries(draxul-control PUBLIC
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(draxul-control PRIVATE advapi32)
endif()

# White-box codec and staged-error tests consume private transport contracts.
# Keep this access test-only; platform handles and backend types remain absent
# from the installed/public control API.
add_library(draxul-control-test-internals INTERFACE)
target_include_directories(draxul-control-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-control-test-internals INTERFACE draxul-control)
