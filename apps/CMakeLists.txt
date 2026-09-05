subdirlist(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT BUILD_CORE_DEMOS)
  list(REMOVE_ITEM SUBDIRS core_demos)
endif()
if(NOT BUILD_DICOM_VIEWER)
  list(REMOVE_ITEM SUBDIRS dicom_viewer)
endif()
if(NOT BUILD_VOLUME_DEMOS)
  list(REMOVE_ITEM SUBDIRS volume_demos)
endif()
if(NOT BUILD_PROTOTYPES)
  list(REMOVE_ITEM SUBDIRS dawn_probe fibers_prototype)
endif()


include_directories(${CMAKE_SOURCE_DIR}/core ${CMAKE_SOURCE_DIR}/core/sources)

foreach(subdir ${SUBDIRS})
  add_subdirectory(${subdir})
endforeach()
