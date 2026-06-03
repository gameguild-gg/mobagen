file(
  GLOB
  RMLUIDEMO_INC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

file(
  GLOB
  RMLUIDEMO_SRC
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

add_executable(rmluidemo ${RMLUIDEMO_SRC} ${RMLUIDEMO_INC})

target_include_directories(rmluidemo PUBLIC ${CORE_INC_DIR})
target_link_libraries(rmluidemo PUBLIC core)
