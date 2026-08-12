add_library(draxul-scoreview-plugin MODULE src/scoreview_plugin.cpp)
target_include_directories(draxul-scoreview-plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/draxul-plugin/include)
target_link_libraries(draxul-scoreview-plugin PRIVATE
    draxul-scoreview-runtime
    nlohmann_json::nlohmann_json
    imgui)
set_target_properties(draxul-scoreview-plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "draxul-scoreview")
if(APPLE)
    set_target_properties(draxul-scoreview-plugin PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path")
endif()
