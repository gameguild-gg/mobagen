option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Test executables ----

set(FOUNDATION_TEST_SOURCES
    Test.cpp
    benchmark_runner_tests.cpp
    binary_reader_tests.cpp
    ecs_tests.cpp
    jobs_tests.cpp
    messaging_tests.cpp
    reactive_tests.cpp
    scene_serialize_tests.cpp
    scene_tests.cpp
    subsystems_tests.cpp
    volume_file_tests.cpp
)

add_executable(FoundationTests ${FOUNDATION_TEST_SOURCES})
target_compile_features(FoundationTests PRIVATE cxx_std_20)
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
          mobagen::core_net
          mobagen::core_scene
          mobagen::core_camera
          mobagen::core_input
          mobagen::core_render
          mobagen::core_resource
          mobagen::volume
)
mobagen_enable_sanitizers(FoundationTests mobagen_core_jobs)

# enable compiler warnings if(NOT TEST_INSTALLED_VERSION) if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
# OR CMAKE_CXX_COMPILER_ID MATCHES "GNU") target_compile_options(core PUBLIC -Wall -Wpedantic
# -Wextra) target_compile_options(core PUBLIC -Wall -Wpedantic -Wextra -Werror) elseif(MSVC)
# target_compile_options(core PUBLIC /W4 /WX) target_compile_definitions(CoreTests PUBLIC
# DOCTEST_CONFIG_USE_STD_HEADERS) endif() endif()

# ---- Discover tests ----

enable_testing()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(FoundationTests TEST_PREFIX "foundation::" PROPERTIES LABELS foundation)

# Preserve the historical build target while keeping dependency boundaries explicit.
add_custom_target(CoreTests DEPENDS FoundationTests)

if(TARGET mobagen::core_app)
  add_executable(AppHostTests Test.cpp app_host_tests.cpp)
  target_compile_features(AppHostTests PRIVATE cxx_std_23)
  target_link_libraries(AppHostTests PRIVATE doctest::doctest mobagen::core_app)
  mobagen_enable_sanitizers(AppHostTests mobagen_core_app)
  doctest_discover_tests(AppHostTests TEST_PREFIX "app-host::" PROPERTIES LABELS app-host)
  add_dependencies(CoreTests AppHostTests)
endif()

# ---- code coverage ----

include(../cmake/coverage.cmake)

mobagen_enable_coverage(FoundationTests mobagen_core_jobs)
if(TARGET AppHostTests)
  mobagen_enable_coverage(AppHostTests mobagen_core_app)
endif()
