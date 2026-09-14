option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

add_library(MobagenReferencePlugin MODULE plugins/reference_plugin.c)
target_link_libraries(MobagenReferencePlugin PRIVATE mobagen::core_plugins)
target_compile_definitions(MobagenReferencePlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
set_target_properties(MobagenReferencePlugin PROPERTIES PREFIX "")

add_library(MobagenMissingEntryPlugin MODULE plugins/missing_entry_plugin.c)
set_target_properties(MobagenMissingEntryPlugin PROPERTIES PREFIX "")

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Test executables ----

set(FOUNDATION_TEST_SOURCES
    Test.cpp
    asset_cache_tests.cpp
    asset_dependency_graph_tests.cpp
    asset_id_tests.cpp
    asset_pipeline_tests.cpp
    asset_registry_tests.cpp
    benchmark_runner_tests.cpp
    binary_reader_tests.cpp
    ecs_tests.cpp
    jobs_tests.cpp
    messaging_tests.cpp
    module_descriptor_tests.cpp
    module_execution_graph_tests.cpp
    module_lifecycle_tests.cpp
    module_registry_tests.cpp
    module_resolver_tests.cpp
    plugin_abi_c_test.c
    plugin_abi_tests.cpp
    plugin_contract_tests.cpp
    plugin_host_tests.cpp
    plugin_loader_tests.cpp
    plugin_package_tests.cpp
    reactive_tests.cpp
    scene_serialize_tests.cpp
    scene_tests.cpp
    subsystems_tests.cpp
    volume_file_tests.cpp
)

if(TARGET mobagen::asset_cli)
  list(APPEND FOUNDATION_TEST_SOURCES asset_cli_tests.cpp)
endif()
if(TARGET mobagen::module_manifest)
  list(APPEND FOUNDATION_TEST_SOURCES module_composition_tests.cpp module_manifest_tests.cpp)
endif()
if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  list(APPEND FOUNDATION_TEST_SOURCES module_lockfile_tests.cpp)
endif()

add_executable(FoundationTests ${FOUNDATION_TEST_SOURCES})
add_dependencies(FoundationTests MobagenMissingEntryPlugin MobagenReferencePlugin)
target_compile_features(FoundationTests PRIVATE cxx_std_20)
target_compile_definitions(
  FoundationTests
  PRIVATE MOBAGEN_MISSING_ENTRY_PLUGIN_PATH="$<TARGET_FILE:MobagenMissingEntryPlugin>"
          MOBAGEN_REFERENCE_PLUGIN_PATH="$<TARGET_FILE:MobagenReferencePlugin>"
)
target_include_directories(
  FoundationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../benchmarks
                          ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
)
target_link_libraries(
  FoundationTests
  PRIVATE doctest::doctest
          mobagen::core_ecs
          mobagen::core_jobs
          mobagen::core_reactive
          mobagen::core_messaging
          mobagen::core_modules
          mobagen::core_plugins
          mobagen::core_net
          mobagen::core_scene
          mobagen::core_camera
          mobagen::core_input
          mobagen::core_render
          mobagen::core_resource
          mobagen::core_assets
          mobagen::volume
)
if(TARGET mobagen::asset_cli)
  target_link_libraries(FoundationTests PRIVATE mobagen::asset_cli)
endif()
if(TARGET mobagen::module_manifest)
  target_include_directories(
    FoundationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../apps/module-compositions
  )
  target_link_libraries(
    FoundationTests PRIVATE mobagen::headless_composition mobagen::module_manifest
  )
endif()
if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  target_link_libraries(FoundationTests PRIVATE mobagen::module_lockfile)
endif()

set(FOUNDATION_COMPILED_TARGETS mobagen_core_assets mobagen_core_jobs mobagen_core_modules
                                mobagen_core_plugins
)
if(TARGET mobagen_asset_cli)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_asset_cli)
endif()
if(TARGET mobagen_headless_composition)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_headless_composition)
endif()
if(TARGET mobagen_module_manifest)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_module_manifest)
endif()
if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_module_lockfile)
endif()
mobagen_enable_sanitizers(FoundationTests ${FOUNDATION_COMPILED_TARGETS})

# enable compiler warnings if(NOT TEST_INSTALLED_VERSION) if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
# OR CMAKE_CXX_COMPILER_ID MATCHES "GNU") target_compile_options(core PUBLIC -Wall -Wpedantic
# -Wextra) target_compile_options(core PUBLIC -Wall -Wpedantic -Wextra -Werror) elseif(MSVC)
# target_compile_options(core PUBLIC /W4 /WX) target_compile_definitions(CoreTests PUBLIC
# DOCTEST_CONFIG_USE_STD_HEADERS) endif() endif()

# ---- Discover tests ----

enable_testing()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(FoundationTests TEST_PREFIX "foundation::" PROPERTIES LABELS foundation)
add_test(NAME foundation::dawn-configure-patch
         COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dawn_patch_test.cmake
)
set_tests_properties(foundation::dawn-configure-patch PROPERTIES LABELS foundation)

# Preserve the historical build target while keeping dependency boundaries explicit.
add_custom_target(CoreTests DEPENDS FoundationTests)

if(TARGET mobagen::core_app)
  add_executable(AppHostTests Test.cpp app_host_tests.cpp module_graphics_composition_tests.cpp)
  target_compile_features(AppHostTests PRIVATE cxx_std_23)
  target_include_directories(
    AppHostTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../apps/module-compositions
  )
  target_link_libraries(
    AppHostTests PRIVATE doctest::doctest mobagen::core_app mobagen::graphics_composition
  )
  mobagen_enable_sanitizers(AppHostTests mobagen_core_app mobagen_graphics_composition)
  doctest_discover_tests(AppHostTests TEST_PREFIX "app-host::" PROPERTIES LABELS app-host)
  add_dependencies(CoreTests AppHostTests)
endif()

# ---- code coverage ----

include(../cmake/coverage.cmake)

mobagen_enable_coverage(FoundationTests ${FOUNDATION_COMPILED_TARGETS})
if(TARGET AppHostTests)
  mobagen_enable_coverage(AppHostTests mobagen_core_app mobagen_graphics_composition)
endif()
