include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(MOBAGEN_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${MOBAGEN_GENERATED_INCLUDE_DIR}/mobagen")
configure_file(cmake/version.h.in "${MOBAGEN_GENERATED_INCLUDE_DIR}/mobagen/version.h" @ONLY)

add_library(mobagen_plugin_sdk INTERFACE)
add_library(mobagen::plugin_sdk ALIAS mobagen_plugin_sdk)
set_target_properties(mobagen_plugin_sdk PROPERTIES EXPORT_NAME plugin_sdk)
target_compile_features(mobagen_plugin_sdk INTERFACE c_std_11)
target_include_directories(
  mobagen_plugin_sdk
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${MOBAGEN_GENERATED_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set(MOBAGEN_PLUGIN_SDK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/MobagenPluginSdk")

configure_package_config_file(
  cmake/MobagenPluginSdkConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/MobagenPluginSdkConfig.cmake"
  INSTALL_DESTINATION "${MOBAGEN_PLUGIN_SDK_INSTALL_CMAKEDIR}"
  NO_SET_AND_CHECK_MACRO NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/MobagenPluginSdkConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)

install(
  TARGETS mobagen_plugin_sdk
  EXPORT MobagenPluginSdkTargets
  COMPONENT MobagenPluginSdk
)
install(
  DIRECTORY include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT MobagenPluginSdk
)
install(
  FILES "${MOBAGEN_GENERATED_INCLUDE_DIR}/mobagen/version.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/mobagen"
  COMPONENT MobagenPluginSdk
)
install(
  EXPORT MobagenPluginSdkTargets
  FILE MobagenPluginSdkTargets.cmake
  NAMESPACE mobagen::
  DESTINATION "${MOBAGEN_PLUGIN_SDK_INSTALL_CMAKEDIR}"
  COMPONENT MobagenPluginSdk
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/MobagenPluginSdkConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/MobagenPluginSdkConfigVersion.cmake"
  DESTINATION "${MOBAGEN_PLUGIN_SDK_INSTALL_CMAKEDIR}"
  COMPONENT MobagenPluginSdk
)
