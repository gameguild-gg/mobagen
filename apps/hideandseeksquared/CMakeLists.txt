file(GLOB HIDEANDSEEK_INC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h
     ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

file(GLOB HIDEANDSEEK_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

install(FILES ${HIDEANDSEEK_INC} DESTINATION include/hideandseek)

add_executable(hideandseek ${HIDEANDSEEK_SRC} ${HIDEANDSEEK_INC})

target_include_directories(hideandseek PUBLIC ${CORE_INC_DIR})
target_link_libraries(hideandseek PUBLIC core)
