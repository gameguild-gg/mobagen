file(GLOB MAZE_INC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.hpp
        )

file(GLOB MAZE_SRC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.c
        )

install (FILES ${MAZE_INC} DESTINATION include/maze)

add_executable(maze ${MAZE_SRC} ${MAZE_INC})

target_include_directories(maze PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries (maze PUBLIC SDL2-static IMGUI core)