file(GLOB LIFE_INC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.hpp
        )

file(GLOB LIFE_SRC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/rules/*.c
        )


install (FILES ${LIFE_INC} DESTINATION include/life)

add_executable(life ${LIFE_SRC} ${LIFE_INC})

target_include_directories(life PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries (life PUBLIC SDL2-static IMGUI core)
