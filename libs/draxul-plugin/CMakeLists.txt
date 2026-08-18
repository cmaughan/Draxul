add_library(draxul-plugin
    src/plugin_manager.cpp
)

target_include_directories(draxul-plugin PUBLIC include)

target_link_libraries(draxul-plugin PUBLIC
    Draxul::PluginSDK
    draxul-types
)
target_link_libraries(draxul-plugin PRIVATE
    draxul-performance
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
)

if(WIN32)
    target_link_libraries(draxul-plugin PRIVATE kernel32)
elseif(APPLE OR UNIX)
    target_link_libraries(draxul-plugin PRIVATE ${CMAKE_DL_LIBS})
endif()
