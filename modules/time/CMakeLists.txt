# set(BUILD_SHARED_LIBS FALSE)

# todo: isolate interfaces in a folder so you can use other time implementations
file(
  GLOB_RECURSE TIME_INC CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.h *.hpp
)

file(
  GLOB_RECURSE TIME_SRC CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.c *.cpp
)

set(TIME_INC_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}
    CACHE PATH "TIME INCLUDE SOURCE PATH"
)

add_library(time STATIC ${TIME_SRC} ${TIME_INC})

install(TARGETS time DESTINATION lib)
install(FILES ${TIME_INC} DESTINATION include/time)
