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

target_include_directories(flocking PUBLIC ${CORE_INC_DIR})
target_link_libraries(flocking PUBLIC core)

# Formal fixture test runner (console; drives the existing rule classes)
if(NOT EMSCRIPTEN)
  CPMAddPackage("gh:onqtam/doctest@2.4.12")

  # The IMGUI target publicly links SDL3 + dawn, which a console test runner must not pull in. Build
  # the minimal core-only imgui static library instead (no backends, no SDL/Dawn dependencies).
  add_library(
    flocking_tests_imgui STATIC
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  )
  target_include_directories(flocking_tests_imgui PUBLIC ${IMGUI_SOURCE_DIR})

  add_executable(
    flocking-tests tests/flocking_tests.cpp behaviours/FlockingRule.cpp behaviours/CohesionRule.cpp
                   behaviours/AlignmentRule.cpp behaviours/SeparationRule.cpp utils/ImGuiExtra.cpp
  )
  target_include_directories(flocking-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CORE_INC_DIR})
  target_link_libraries(flocking-tests PRIVATE doctest::doctest glm::glm flocking_tests_imgui)
  # Normalize to forward slashes: MSVC mangles backslash escapes inside string-literal defines.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests" FLOCKING_TESTS_DIR_PATH)
  target_compile_definitions(flocking-tests PRIVATE FLOCKING_TESTS_DIR="${FLOCKING_TESTS_DIR_PATH}")
endif()
