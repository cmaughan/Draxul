add_library(draxul-types
    src/log.cpp
    src/process_util.cpp
    src/runtime_path.cpp
)
target_include_directories(draxul-types PUBLIC include)
target_link_libraries(draxul-types PUBLIC glm::glm)
target_link_libraries(draxul-types PRIVATE draxul-performance)

# Ship the shared foundation types with the plugin SDK so out-of-tree product
# builds can link them (Slice 3 of plans/shared-library-consolidation.md
# consumes this; no in-tree consumer references the installed copy).
include(GNUInstallDirs)
install(TARGETS draxul-types
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    COMPONENT draxul-plugin-sdk)
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT draxul-plugin-sdk)
