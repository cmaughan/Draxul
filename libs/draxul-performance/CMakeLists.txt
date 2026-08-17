add_library(draxul-performance
    src/perf_timing.cpp
)

target_include_directories(draxul-performance PUBLIC include)

# draxul-types (shipped with the plugin SDK) references these symbols from its
# PERF_MEASURE spans, so the archive must travel with it for out-of-tree
# product builds that link the installed draxul-types.
include(GNUInstallDirs)
install(TARGETS draxul-performance
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    COMPONENT draxul-plugin-sdk)
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT draxul-plugin-sdk)
