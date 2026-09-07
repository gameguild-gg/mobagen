file(
  GLOB
  FLOCKING_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.hpp
)

file(
  GLOB
  FLOCKING_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.cpp
)

install(FILES ${FLOCKING_INC} DESTINATION include/flocking)

add_executable(flocking ${FLOCKING_SRC} ${FLOCKING_INC})

target_link_libraries(flocking PUBLIC core)

# Formal fixture test runner (console; drives the existing rule classes)
if(NOT EMSCRIPTEN)
  add_executable(
    flocking-tests tests/flocking_tests.cpp behaviours/FlockingRule.cpp behaviours/CohesionRule.cpp
                   behaviours/AlignmentRule.cpp behaviours/SeparationRule.cpp utils/ImGuiExtra.cpp
  )
  # App-local includes only; core paths are inherited from apps/CMakeLists.txt.
  target_include_directories(flocking-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  # imgui_core (external/imgui.cmake) provides ImGui headers without the SDL3/dawn deps a console
  # runner must not pull in.
  target_link_libraries(flocking-tests PRIVATE doctest::doctest glm::glm imgui_core)
  # Normalize to forward slashes: MSVC mangles backslash escapes inside string-literal defines.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests" FLOCKING_TESTS_DIR_PATH)
  target_compile_definitions(flocking-tests PRIVATE FLOCKING_TESTS_DIR="${FLOCKING_TESTS_DIR_PATH}")
endif()
