file(
  GLOB
  HIDEANDSEEK_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

file(
  GLOB
  HIDEANDSEEK_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

install(FILES ${HIDEANDSEEK_INC} DESTINATION include/hideandseek)

add_executable(hideandseek ${HIDEANDSEEK_SRC} ${HIDEANDSEEK_INC})

target_include_directories(hideandseek PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries(hideandseek PUBLIC SDL2-static IMGUI core)
