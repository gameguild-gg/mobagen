add_library(
  mobagen_core_modules STATIC capability_registry.cpp catalog_index.cpp descriptor.cpp execution_graph.cpp
                              lifecycle.cpp resolver.cpp
)
add_library(mobagen::core_modules ALIAS mobagen_core_modules)
target_include_directories(mobagen_core_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_core_modules PUBLIC cxx_std_20)

if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  add_library(
    mobagen_module_manifest STATIC catalog_fetcher.cpp catalog_parser.cpp manifest_parser.cpp
                                   module_sync_plan.cpp
  )
  add_library(mobagen::module_manifest ALIAS mobagen_module_manifest)
  target_include_directories(mobagen_module_manifest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(mobagen_module_manifest PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_module_manifest
    PUBLIC mobagen::core_http mobagen::core_modules
    PRIVATE yaml-cpp::yaml-cpp
  )

  add_library(mobagen_module_lockfile STATIC lockfile.cpp lockfile_io.cpp)
  add_library(mobagen::module_lockfile ALIAS mobagen_module_lockfile)
  target_include_directories(mobagen_module_lockfile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_features(mobagen_module_lockfile PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_module_lockfile
    PUBLIC mobagen::core_modules
    PRIVATE mobagen::core_assets
  )
endif()
