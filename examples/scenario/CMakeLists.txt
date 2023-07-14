# GLM
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME GLM
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG master
)
include_directories(${GLM_SOURCE_DIR})
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGLM "${AFTER} - ${BEFORE}")
message(STATUS "GLM TIME: ${DELTAGLM}s")

file(
  GLOB
  SCENARIO_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.hpp
)

file(
  GLOB
  SCENARIO_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.c
)

install(FILES ${SCENARIO_INC} DESTINATION include/scenario)

add_executable(scenario ${SCENARIO_SRC} ${SCENARIO_INC})

target_include_directories(scenario PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries(scenario PUBLIC SDL2-static IMGUI core glm)
