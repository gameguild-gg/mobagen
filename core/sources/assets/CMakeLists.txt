add_library(
  mobagen_core_assets STATIC
  asset_cache.cpp
  asset_cache.hpp
  asset_dependency_graph.cpp
  asset_dependency_graph.hpp
  asset_id.cpp
  asset_id.hpp
  asset_registry.hpp
)
add_library(mobagen::core_assets ALIAS mobagen_core_assets)

target_compile_features(mobagen_core_assets PUBLIC cxx_std_20)
target_include_directories(mobagen_core_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mobagen_core_assets PUBLIC mobagen::core_resource)
