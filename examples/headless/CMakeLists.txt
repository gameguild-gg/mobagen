file(
  GLOB
  HEADLESS_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

file(
  GLOB
  HEADLESS_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

install(FILES ${HEADLESS_INC} DESTINATION include/headless)

add_executable(headless ${HEADLESS_SRC} ${HEADLESS_INC})

target_include_directories(headless PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries(headless PUBLIC SDL2-static IMGUI core)