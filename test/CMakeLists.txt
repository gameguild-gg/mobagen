option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

add_library(MobagenReferencePlugin MODULE plugins/reference_plugin.c)
target_link_libraries(MobagenReferencePlugin PRIVATE mobagen::plugin_sdk)
target_compile_definitions(MobagenReferencePlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
set_target_properties(MobagenReferencePlugin PROPERTIES PREFIX "")

add_library(MobagenMissingEntryPlugin MODULE plugins/missing_entry_plugin.c)
set_target_properties(MobagenMissingEntryPlugin PROPERTIES PREFIX "")

foreach(FAILURE_MODE IN ITEMS 1 2 3)
  add_library(
    MobagenLifecycleFailurePlugin${FAILURE_MODE} MODULE plugins/lifecycle_failure_plugin.c
  )
  target_link_libraries(MobagenLifecycleFailurePlugin${FAILURE_MODE} PRIVATE mobagen::plugin_sdk)
  target_compile_definitions(
    MobagenLifecycleFailurePlugin${FAILURE_MODE} PRIVATE MOBAGEN_PLUGIN_BUILD
                                                         MOBAGEN_TEST_FAILURE_MODE=${FAILURE_MODE}
  )
  set_target_properties(MobagenLifecycleFailurePlugin${FAILURE_MODE} PROPERTIES PREFIX "")
endforeach()

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
    plugin_activation_tests.cpp
    plugin_catalog_tests.cpp
    plugin_host_tests.cpp
    plugin_loader_tests.cpp
    plugin_package_tests.cpp
    plugin_resolution_activation_tests.cpp
    plugin_store_tests.cpp
    portable_plugin_store_tests.cpp
    reactive_tests.cpp
    scene_serialize_tests.cpp
    scene_tests.cpp
    subsystems_tests.cpp
    volume_file_tests.cpp
    wasm_memory_tests.cpp
    wasm_plugin_loader_tests.cpp
    wasm_plugin_contract_tests.cpp
    wasm_runtime_tests.cpp
    wasm_command_channel_tests.cpp
    wasm_host_imports_tests.cpp
)

if(TARGET mobagen::asset_cli)
  list(APPEND FOUNDATION_TEST_SOURCES asset_cli_tests.cpp)
endif()
if(TARGET mobagen::plugin_cli)
  list(APPEND FOUNDATION_TEST_SOURCES plugin_cli_tests.cpp)
endif()
if(TARGET mobagen::project_cli)
  list(APPEND FOUNDATION_TEST_SOURCES project_cli_tests.cpp)
endif()
if(TARGET mobagen::module_manifest)
  list(APPEND FOUNDATION_TEST_SOURCES module_catalog_fetcher_tests.cpp module_catalog_tests.cpp module_composition_tests.cpp module_manifest_tests.cpp
       native_project_tests.cpp
  )
endif()
if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  list(APPEND FOUNDATION_TEST_SOURCES module_lockfile_tests.cpp)
endif()
if(MOBAGEN_WASM_BACKEND_WAMR)
  list(APPEND FOUNDATION_TEST_SOURCES wamr_backend_tests.cpp)
endif()

add_executable(FoundationTests ${FOUNDATION_TEST_SOURCES})
add_dependencies(
  FoundationTests MobagenLifecycleFailurePlugin1 MobagenLifecycleFailurePlugin2
  MobagenLifecycleFailurePlugin3 MobagenMissingEntryPlugin MobagenReferencePlugin
)
target_compile_features(FoundationTests PRIVATE cxx_std_20)
target_compile_definitions(
  FoundationTests
  PRIVATE MOBAGEN_CONFIGURE_FAILURE_PLUGIN_PATH="$<TARGET_FILE:MobagenLifecycleFailurePlugin1>"
          MOBAGEN_START_FAILURE_PLUGIN_PATH="$<TARGET_FILE:MobagenLifecycleFailurePlugin2>"
          MOBAGEN_CAPABILITY_MISMATCH_PLUGIN_PATH="$<TARGET_FILE:MobagenLifecycleFailurePlugin3>"
          MOBAGEN_MISSING_ENTRY_PLUGIN_PATH="$<TARGET_FILE:MobagenMissingEntryPlugin>"
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
if(TARGET mobagen::plugin_cli)
  target_link_libraries(FoundationTests PRIVATE mobagen::plugin_cli)
endif()
if(TARGET mobagen::project_cli)
  target_link_libraries(FoundationTests PRIVATE mobagen::project_cli)
endif()
if(TARGET mobagen::module_manifest)
  target_include_directories(
    FoundationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../apps/module-compositions
  )
  target_link_libraries(
    FoundationTests PRIVATE mobagen::headless_composition mobagen::module_manifest
                            mobagen::native_project mobagen::portable_project
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
if(TARGET mobagen_plugin_cli)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_plugin_cli)
endif()
if(TARGET mobagen_project_cli)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_project_cli)
endif()
if(TARGET mobagen_headless_composition)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_headless_composition)
endif()
if(TARGET mobagen_native_project)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_native_project)
endif()
if(TARGET mobagen_portable_project)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_portable_project)
endif()
if(TARGET mobagen_project_support)
  list(APPEND FOUNDATION_COMPILED_TARGETS mobagen_project_support)
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

add_test(
  NAME foundation::plugin-sdk-installed-consumer
  COMMAND
    ${CMAKE_COMMAND} "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/plugin-sdk-consumer"
    "-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/plugin-sdk-consumer-build"
    "-DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/plugin-sdk-install"
    "-DHOST_BINARY_DIR=${CMAKE_BINARY_DIR}" "-DHOST_CONFIG=$<CONFIG>"
    "-DHOST_GENERATOR=${CMAKE_GENERATOR}" -P
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin_sdk_consumer_test.cmake
)
set_tests_properties(foundation::plugin-sdk-installed-consumer PROPERTIES LABELS foundation)

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
