file(GLOB MAZE_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.hpp
        )

file(GLOB MAZE_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.c
        )


install (FILES ${MAZE_INC} DESTINATION include/maze)

add_executable(maze ${MAZE_SRC} ${MAZE_INC})

target_include_directories(maze PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
IF(EMSCRIPTEN)
    set_target_properties(maze PROPERTIES LINK_FLAGS "-s WASM=0 -s FULL_ES2=1 -s AUTO_JS_LIBRARIES=false -s USE_SDL=0")
ENDIF()

# remove SDL2 references and use only the CORE code wrapping all SDL2 code
target_link_libraries (maze PUBLIC SDL2 IMGUI core)