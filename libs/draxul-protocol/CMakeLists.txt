add_library(draxul-protocol
    src/server_protocol.cpp
)

target_include_directories(draxul-protocol PUBLIC include)

target_compile_definitions(draxul-protocol PRIVATE
    DRAXUL_BUILD_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(draxul-protocol PUBLIC
    nlohmann_json::nlohmann_json
)
