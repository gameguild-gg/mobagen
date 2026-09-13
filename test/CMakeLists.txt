option(ENABLE_TEST_COVERAGE "Enable test coverage" ON)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Test executables ----

set(
  FOUNDATION_TEST_SOURCES
  Test.cpp
  ecs_tests.cpp
  jobs_tests.cpp
  messaging_tests.cpp
  reactive_tests.cpp
  scene_tests.cpp
  subsystems_tests.cpp
)

add_executable(FoundationTests ${FOUNDATION_TEST_SOURCES})
target_compile_features(FoundationTests PRIVATE cxx_std_20)
target_include_directories(FoundationTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources)
target_link_libraries(
  FoundationTests
  PRIVATE doctest::doctest
          mobagen::core_ecs
          mobagen::core_jobs
          mobagen::core_reactive
          mobagen::core_messaging
          mobagen::core_scene
          mobagen::core_camera
          mobagen::core_input
          mobagen::core_render
          mobagen::core_resource
)

# enable compiler warnings if(NOT TEST_INSTALLED_VERSION) if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
# OR CMAKE_CXX_COMPILER_ID MATCHES "GNU") target_compile_options(core PUBLIC -Wall -Wpedantic
# -Wextra) target_compile_options(core PUBLIC -Wall -Wpedantic -Wextra -Werror) elseif(MSVC)
# target_compile_options(core PUBLIC /W4 /WX) target_compile_definitions(CoreTests PUBLIC
# DOCTEST_CONFIG_USE_STD_HEADERS) endif() endif()

# ---- Discover tests ----

enable_testing()

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(
  FoundationTests TEST_PREFIX "foundation::" PROPERTIES LABELS foundation
)

# Preserve the historical build target while keeping dependency boundaries explicit.
add_custom_target(CoreTests DEPENDS FoundationTests)

if(TARGET mobagen::core_app)
  add_executable(AppHostTests Test.cpp app_host_tests.cpp)
  target_compile_features(AppHostTests PRIVATE cxx_std_23)
  target_link_libraries(AppHostTests PRIVATE doctest::doctest mobagen::core_app)
  doctest_discover_tests(AppHostTests TEST_PREFIX "app-host::" PROPERTIES LABELS app-host)
  add_dependencies(CoreTests AppHostTests)
endif()

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(FoundationTests PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(FoundationTests PUBLIC -fprofile-arcs -ftest-coverage)
  if(TARGET AppHostTests)
    target_compile_options(AppHostTests PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
    target_link_options(AppHostTests PUBLIC -fprofile-arcs -ftest-coverage)
  endif()
endif()
