file(
  GLOB
  MAZE_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.hpp
)

file(
  GLOB
  MAZE_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.c
)

install(FILES ${MAZE_INC} DESTINATION include/maze)

add_executable(maze ${MAZE_SRC} ${MAZE_INC})

target_link_libraries(maze PUBLIC core)

# Formal fixture test runner (console; deterministic seeded maze generation)
if(NOT EMSCRIPTEN)
  # The IMGUI target publicly links SDL3 + dawn, which a console test runner must not pull in. Build
  # the minimal core-only imgui static library instead (no backends, no SDL/Dawn dependencies).
  add_library(
    maze_tests_imgui STATIC
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
  )
  target_include_directories(maze_tests_imgui PUBLIC ${IMGUI_SOURCE_DIR})

  add_executable(
    maze-tests
    tests/maze_tests.cpp
    World.cpp
    MazeGeneratorBase.cpp
    generators/RecursiveBacktrackerExample.cpp
    generators/PrimExample.cpp
    generators/HuntAndKillExample.cpp
  )
  # App-local includes only; core paths are inherited from apps/CMakeLists.txt.
  target_include_directories(maze-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(maze-tests PRIVATE doctest::doctest glm::glm maze_tests_imgui)
  # Normalize to forward slashes: MSVC mangles backslash escapes inside string-literal defines.
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests" MAZE_TESTS_DIR_PATH)
  target_compile_definitions(maze-tests PRIVATE MAZE_TESTS_DIR="${MAZE_TESTS_DIR_PATH}")
endif()
