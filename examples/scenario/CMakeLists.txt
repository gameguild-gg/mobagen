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
target_link_libraries (scenario PUBLIC SDL2-static IMGUI core)
IF(EMSCRIPTEN)
    target_compile_options(scenario PUBLIC "-O0")
    target_link_options(scenario PUBLIC "-s WASM=0 -s FULL_ES2=1 -s USE_SDL=0 -s EXPORT_ALL=1")
ENDIF()