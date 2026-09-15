add_library(mobagen_catalog_publisher STATIC catalog_publisher.cpp catalog_publisher.hpp)
add_library(mobagen::catalog_publisher ALIAS mobagen_catalog_publisher)
target_compile_features(mobagen_catalog_publisher PUBLIC cxx_std_20)
target_include_directories(mobagen_catalog_publisher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  mobagen_catalog_publisher
  PUBLIC mobagen::module_manifest mobagen::plugin_sdk
  PRIVATE mobagen::core_assets mobagen::core_plugins mobagen::module_lockfile
)

add_library(mobagen_catalog_cli STATIC catalog_cli.cpp catalog_cli.hpp)
add_library(mobagen::catalog_cli ALIAS mobagen_catalog_cli)
target_compile_features(mobagen_catalog_cli PUBLIC cxx_std_20)
target_include_directories(mobagen_catalog_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mobagen_catalog_cli PUBLIC mobagen::catalog_publisher)

add_executable(MobagenCatalog main.cpp)
target_compile_features(MobagenCatalog PRIVATE cxx_std_20)
target_link_libraries(MobagenCatalog PRIVATE mobagen::catalog_cli)

if(WIN32)
  set(MOBAGEN_CATALOG_TARGET windows)
elseif(APPLE)
  set(MOBAGEN_CATALOG_TARGET macos)
else()
  set(MOBAGEN_CATALOG_TARGET linux)
endif()

set(MOBAGEN_CATALOG_BASE_URL
    "https://plugins.mobagen.dev/v1"
    CACHE STRING "Base URL embedded in generated first-party module catalogs"
)

set(MOBAGEN_RECOMMENDED_NATIVE_PLUGINS "")
if(TARGET MobagenDefaultAssetStorePlugin)
  list(APPEND MOBAGEN_RECOMMENDED_NATIVE_PLUGINS "$<TARGET_FILE:MobagenDefaultAssetStorePlugin>")
endif()
if(TARGET MobagenWindowSdl3Plugin)
  list(APPEND MOBAGEN_RECOMMENDED_NATIVE_PLUGINS "$<TARGET_FILE:MobagenWindowSdl3Plugin>")
endif()
if(TARGET MobagenRenderWebgpuPlugin)
  list(APPEND MOBAGEN_RECOMMENDED_NATIVE_PLUGINS "$<TARGET_FILE:MobagenRenderWebgpuPlugin>")
endif()

if(MOBAGEN_RECOMMENDED_NATIVE_PLUGINS)
  set(MOBAGEN_RECOMMENDED_CATALOG_ARGS "")
  foreach(plugin IN LISTS MOBAGEN_RECOMMENDED_NATIVE_PLUGINS)
    list(APPEND MOBAGEN_RECOMMENDED_CATALOG_ARGS --plugin "${plugin}")
  endforeach()
  set(MOBAGEN_RECOMMENDED_CATALOG_DEPENDS MobagenCatalog)
  foreach(plugin_target IN ITEMS MobagenDefaultAssetStorePlugin MobagenWindowSdl3Plugin
                                 MobagenRenderWebgpuPlugin
  )
    if(TARGET ${plugin_target})
      list(APPEND MOBAGEN_RECOMMENDED_CATALOG_DEPENDS ${plugin_target})
    endif()
  endforeach()
  add_custom_target(
    MobagenRecommendedCatalog
    COMMAND
      $<TARGET_FILE:MobagenCatalog> publish "${CMAKE_BINARY_DIR}/module-catalog/$<CONFIG>"
      --base-url "${MOBAGEN_CATALOG_BASE_URL}" --target "${MOBAGEN_CATALOG_TARGET}"
      ${MOBAGEN_RECOMMENDED_CATALOG_ARGS}
    DEPENDS ${MOBAGEN_RECOMMENDED_CATALOG_DEPENDS}
    COMMAND_EXPAND_LISTS VERBATIM
  )
endif()
