add_library(mobagen_asset_cli STATIC asset_cli.cpp asset_cli.hpp)
add_library(mobagen::asset_cli ALIAS mobagen_asset_cli)
target_compile_features(mobagen_asset_cli PUBLIC cxx_std_20)
target_include_directories(mobagen_asset_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mobagen_asset_cli PUBLIC mobagen::core_assets)

add_executable(MobagenAssets main.cpp)
target_compile_features(MobagenAssets PRIVATE cxx_std_20)
target_link_libraries(MobagenAssets PRIVATE mobagen::asset_cli)
