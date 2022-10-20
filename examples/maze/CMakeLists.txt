file(GLOB MAZE_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        )

file(GLOB MAZE_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        )


install (FILES ${MAZE_INC} DESTINATION include/maze)

add_executable(maze ${MAZE_SRC} ${MAZE_INC} MazeGenerator.cpp MazeGenerator.h)

target_include_directories(maze PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})

# remove SDL2 references and use only the CORE code wrapping all SDL2 code
target_link_libraries (maze PUBLIC SDL2 IMGUI core)