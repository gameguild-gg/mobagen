option(ENABLE_TEST_COVERAGE "Enable test coverage" ON)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../external/cpm.cmake)

CPMAddPackage("gh:onqtam/doctest@2.4.12")
CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")

# if(TEST_INSTALLED_VERSION) find_package(Concurrency REQUIRED) else() CPMAddPackage(NAME
# Concurrency SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..) endif()

# ---- Create binary ----

file(GLOB sourcestest CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(CoreTests ${sourcestest})
target_link_libraries(CoreTests doctest::doctest core) # cpr)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../core/)
set_target_properties(CoreTests PROPERTIES CXX_STANDARD 20)

# enable compiler warnings if(NOT TEST_INSTALLED_VERSION) if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
# OR CMAKE_CXX_COMPILER_ID MATCHES "GNU") target_compile_options(core PUBLIC -Wall -Wpedantic
# -Wextra) target_compile_options(core PUBLIC -Wall -Wpedantic -Wextra -Werror) elseif(MSVC)
# target_compile_options(core PUBLIC /W4 /WX) target_compile_definitions(CoreTests PUBLIC
# DOCTEST_CONFIG_USE_STD_HEADERS) endif() endif()

# ---- Add CoreTests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(CoreTests)

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(core PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(core PUBLIC -fprofile-arcs -ftest-coverage)
endif()
