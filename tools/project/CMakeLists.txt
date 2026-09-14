add_library(mobagen_project_cli STATIC project_cli.cpp project_cli.hpp)
add_library(mobagen::project_cli ALIAS mobagen_project_cli)
target_compile_features(mobagen_project_cli PUBLIC cxx_std_20)
target_include_directories(mobagen_project_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
  mobagen_project_cli
  PUBLIC mobagen::native_project
  PRIVATE mobagen::module_distribution mobagen::portable_project mobagen::project_support
)
if(MOBAGEN_WASM_BACKEND_WAMR)
  target_compile_definitions(mobagen_project_cli PRIVATE MOBAGEN_PROJECT_CLI_HAS_WAMR=1)
endif()
if(TARGET mobagen::http_curl)
  target_compile_definitions(mobagen_project_cli PRIVATE MOBAGEN_PROJECT_CLI_HAS_CURL=1)
  target_link_libraries(mobagen_project_cli PRIVATE mobagen::http_curl)
endif()

add_executable(MobagenProject main.cpp)
target_compile_features(MobagenProject PRIVATE cxx_std_20)
target_link_libraries(MobagenProject PRIVATE mobagen::project_cli)
