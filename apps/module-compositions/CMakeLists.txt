add_subdirectory(headless)

if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  add_library(mobagen_project_support STATIC project_support.cpp project_support.hpp)
  add_library(mobagen::project_support ALIAS mobagen_project_support)
  target_include_directories(mobagen_project_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(mobagen_project_support PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_project_support
    PUBLIC mobagen::module_manifest
    PRIVATE mobagen::core_assets
  )

  add_subdirectory(portable)
  if(NOT EMSCRIPTEN)
    add_subdirectory(native)
  endif()
endif()

if(MOBAGEN_BUILD_RUNTIME)
  add_subdirectory(graphics)
endif()
