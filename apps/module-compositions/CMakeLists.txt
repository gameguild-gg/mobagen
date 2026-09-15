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

  add_library(
    mobagen_project_bootstrap STATIC project_bootstrap.cpp project_bootstrap.hpp
  )
  add_library(mobagen::project_bootstrap ALIAS mobagen_project_bootstrap)
  target_include_directories(
    mobagen_project_bootstrap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_features(mobagen_project_bootstrap PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_project_bootstrap
    PUBLIC mobagen::module_distribution mobagen::module_lockfile
           mobagen::plugin_sdk
    PRIVATE mobagen::project_support
  )

  add_library(
    mobagen_project_module_manager STATIC project_module_manager.cpp
                                          project_module_manager.hpp
  )
  add_library(mobagen::project_module_manager ALIAS mobagen_project_module_manager)
  target_include_directories(
    mobagen_project_module_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_features(mobagen_project_module_manager PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_project_module_manager
    PUBLIC mobagen::portable_project
    PRIVATE mobagen::project_support
  )
  if(TARGET mobagen::native_project)
    target_compile_definitions(
      mobagen_project_module_manager
      PRIVATE MOBAGEN_PROJECT_MODULE_MANAGER_HAS_NATIVE=1
    )
    target_link_libraries(
      mobagen_project_module_manager PUBLIC mobagen::native_project
    )
  endif()

  add_library(
    mobagen_project_startup STATIC project_startup.cpp project_startup.hpp
  )
  add_library(mobagen::project_startup ALIAS mobagen_project_startup)
  target_include_directories(
    mobagen_project_startup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_compile_features(mobagen_project_startup PUBLIC cxx_std_20)
  target_link_libraries(
    mobagen_project_startup
    PUBLIC mobagen::project_bootstrap mobagen::project_module_manager
  )
endif()

if(MOBAGEN_BUILD_RUNTIME)
  add_subdirectory(graphics)
endif()
