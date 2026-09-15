add_library(
  mobagen_portable_project STATIC locked_project.cpp locked_project.hpp module_manager.cpp
                                  module_manager.hpp project_runtime.cpp project_runtime.hpp
)
add_library(mobagen::portable_project ALIAS mobagen_portable_project)
target_include_directories(mobagen_portable_project PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(mobagen_portable_project PUBLIC cxx_std_20)
target_link_libraries(
  mobagen_portable_project
  PUBLIC mobagen::core_plugins mobagen::module_distribution mobagen::module_manifest
         mobagen::module_lockfile
  PRIVATE mobagen::project_support
)
