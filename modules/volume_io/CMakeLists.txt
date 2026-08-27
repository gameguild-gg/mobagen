add_library(mobagen_volume_io STATIC sources/volume_io.cpp)
add_library(mobagen::volume_io ALIAS mobagen_volume_io)
target_include_directories(mobagen_volume_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_compile_features(mobagen_volume_io PUBLIC cxx_std_20)

if(USE_GDCM)
  target_compile_definitions(mobagen_volume_io PRIVATE HAVE_GDCM)
  target_link_libraries(
    mobagen_volume_io PUBLIC gdcm_headers gdcmMSFF gdcmDSED gdcmDICT gdcmIOD gdcmCommon
  )
endif()
