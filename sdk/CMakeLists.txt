include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(draxul-plugin-sdk INTERFACE)
add_library(Draxul::PluginSDK ALIAS draxul-plugin-sdk)

target_include_directories(draxul-plugin-sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(draxul-plugin-sdk PROPERTIES
    EXPORT_NAME PluginSDK)

set(DRAXUL_PLUGIN_SDK_CMAKE_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/DraxulPluginSDK")

configure_package_config_file(
    cmake/DraxulPluginSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/DraxulPluginSDKConfig.cmake"
    INSTALL_DESTINATION "${DRAXUL_PLUGIN_SDK_CMAKE_DIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/DraxulPluginSDKConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(TARGETS draxul-plugin-sdk
    EXPORT DraxulPluginSDKTargets
    COMPONENT draxul-plugin-sdk)
install(DIRECTORY include/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT draxul-plugin-sdk)
install(EXPORT DraxulPluginSDKTargets
    FILE DraxulPluginSDKTargets.cmake
    NAMESPACE Draxul::
    DESTINATION "${DRAXUL_PLUGIN_SDK_CMAKE_DIR}"
    COMPONENT draxul-plugin-sdk)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/DraxulPluginSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/DraxulPluginSDKConfigVersion.cmake"
    DESTINATION "${DRAXUL_PLUGIN_SDK_CMAKE_DIR}"
    COMPONENT draxul-plugin-sdk)
