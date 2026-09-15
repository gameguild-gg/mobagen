cmake_minimum_required(VERSION 3.16.3 FATAL_ERROR)
project(MobagenCpmLegacyConsumer LANGUAGES C CXX)

foreach(REQUIRED_VARIABLE IN ITEMS MOBAGEN_SOURCE_DIR CPM_FILE)
  if(NOT DEFINED ${REQUIRED_VARIABLE} OR "${${REQUIRED_VARIABLE}}" STREQUAL "")
    message(FATAL_ERROR "${REQUIRED_VARIABLE} is required")
  endif()
endforeach()

include("${CPM_FILE}")
CPMAddPackage(
  NAME Mobagen
  SOURCE_DIR "${MOBAGEN_SOURCE_DIR}"
  OPTIONS "MOBAGEN_BUILD_RUNTIME OFF" "MOBAGEN_BUILD_BUNDLED_MODULES OFF"
          "MOBAGEN_BUILD_MANIFEST_TOOLS OFF" "MOBAGEN_HTTP_BACKEND_CURL OFF"
          "MOBAGEN_BUILD_ASSET_TOOLS OFF" "MOBAGEN_BUILD_PLUGIN_TOOLS OFF"
          "MOBAGEN_BUILD_CATALOG_TOOLS OFF" "MOBAGEN_BUILD_MODULE_COMPOSITIONS OFF"
          "MOBAGEN_BUILD_PROJECT_TOOLS OFF" "MOBAGEN_BUILD_BENCHMARKS OFF"
          "BUILD_TESTING OFF" "BUILD_EXAMPLES OFF" "ENABLE_DOCUMENTATION OFF"
          "ENABLE_TEST_COVERAGE OFF"
)

if(NOT TARGET core OR NOT TARGET mobagen::core OR NOT TARGET mobagen::core_ecs)
  message(FATAL_ERROR "Mobagen no longer exposes its legacy core targets through CPM")
endif()
if(NOT MOBAGEN_BUILD_BUILTIN_SUBSYSTEMS)
  message(FATAL_ERROR "The backward-compatible built-in subsystem default changed")
endif()

add_executable(MobagenLegacyCoreConsumer main.cpp)
target_compile_features(MobagenLegacyCoreConsumer PRIVATE cxx_std_20)
target_link_libraries(MobagenLegacyCoreConsumer PRIVATE mobagen::core)

enable_testing()
add_test(NAME legacy-core-runs COMMAND MobagenLegacyCoreConsumer)
