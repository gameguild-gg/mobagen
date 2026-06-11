subdirlist(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR})

list(REMOVE_ITEM SUBDIRS opengl_renderer)

if(NOT USE_GDCM)
  list(REMOVE_ITEM SUBDIRS volume_io)
endif()

foreach(subdir ${SUBDIRS})
  add_subdirectory(${subdir})
endforeach()
