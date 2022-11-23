
file(GLOB FLOCKING_INC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.hpp
        )

file(GLOB FLOCKING_SRC
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/behaviours/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/gameobjects/*.cpp
        )


install (FILES ${FLOCKING_INC} DESTINATION include/flocking)

add_executable(flocking ${FLOCKING_SRC} ${FLOCKING_INC})

target_include_directories(flocking PUBLIC ${SDL2_INCLUDE_DIR} ${CORE_INC_DIR})
target_link_libraries (flocking PUBLIC SDL2-static IMGUI core)
