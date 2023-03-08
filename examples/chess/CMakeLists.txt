file(
  GLOB
  CHESS_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pieces/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/pieces/*.hpp
)

file(
  GLOB
  CHESS_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/pieces/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/pieces/*.c
)

install(FILES ${CHESS_INC} DESTINATION include/chess)

add_executable(chess ${CHESS_SRC} ${CHESS_INC})

target_include_directories(chess PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries(chess PUBLIC SDL2::SDL2 SDL2_image::SDL2_image-static IMGUI core)
