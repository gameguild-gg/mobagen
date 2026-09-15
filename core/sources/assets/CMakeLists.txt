add_library(
  mobagen_core_assets STATIC
  asset_cache.cpp
  asset_cache.hpp
  asset_dependency_graph.cpp
  asset_dependency_graph.hpp
  asset_id.cpp
  asset_id.hpp
  asset_manager.hpp
  asset_pipeline.cpp
  asset_pipeline.hpp
  asset_registry.hpp
)
add_library(mobagen::core_assets ALIAS mobagen_core_assets)

target_compile_features(mobagen_core_assets PUBLIC cxx_std_20)
target_include_directories(mobagen_core_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_core_assets PUBLIC mobagen::core_resource)

add_library(mobagen_asset_store_service STATIC asset_store_service.cpp asset_store_service.hpp)
add_library(mobagen::asset_store_service ALIAS mobagen_asset_store_service)
target_compile_features(mobagen_asset_store_service PUBLIC cxx_std_20)
target_include_directories(mobagen_asset_store_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_asset_store_service PUBLIC mobagen::core_assets mobagen::plugin_sdk)
