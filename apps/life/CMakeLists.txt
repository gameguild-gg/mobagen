file(
  GLOB
  LIFE_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/fsm/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/fsm/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.hpp
)

file(
  GLOB
  LIFE_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.c
)

install(FILES ${LIFE_INC} DESTINATION include/life)

add_executable(life ${LIFE_SRC} ${LIFE_INC})

target_link_libraries(life PUBLIC core)

# Formal fixture test runner (console; drives the existing rule classes)
if(NOT EMSCRIPTEN)
  CPMAddPackage("gh:onqtam/doctest@2.4.12")

  add_executable(life-tests tests/life_tests.cpp World.cpp rules/JohnConway.cpp)
  # App-local includes only; core paths are inherited from apps/CMakeLists.txt.
  target_include_directories(life-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # The rule constructors log through SDL when wiring is missing.
  target_link_libraries(life-tests PRIVATE doctest::doctest glm::glm SDL3::SDL3-static)
  # Normalize to forward slashes: MSVC mangles backslash escapes inside string-literal defines.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests" LIFE_TESTS_DIR_PATH)
  target_compile_definitions(life-tests PRIVATE LIFE_TESTS_DIR="${LIFE_TESTS_DIR_PATH}")
endif()
