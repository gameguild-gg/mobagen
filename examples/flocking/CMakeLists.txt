
file(GLOB FLOCKING_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp)

file(GLOB FLOCKING_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        )


install (FILES ${FLOCKING_INC} DESTINATION include/flocking)

add_executable(flocking ${FLOCKING_SRC} ${FLOCKING_INC})

target_include_directories(flocking PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})

# remove SDL2 references and use only the CORE code wrapping all SDL2 code
target_link_libraries (flocking PUBLIC SDL2 IMGUI core)