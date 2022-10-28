file(GLOB SCENARIO_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.hpp
        )

file(GLOB SCENARIO_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/generators/*.c
        )


install (FILES ${SCENARIO_INC} DESTINATION include/scenario)

add_executable(scenario ${SCENARIO_SRC} ${SCENARIO_INC})

target_include_directories(scenario PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})

# remove SDL2 references and use only the CORE code wrapping all SDL2 code
target_link_libraries(scenario PUBLIC SDL2 IMGUI core)