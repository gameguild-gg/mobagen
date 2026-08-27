add_executable(volume_buffer_test sources/volume_buffer_test.cpp)
target_link_libraries(volume_buffer_test PRIVATE mobagen::volume)

if(USE_GDCM)
  add_executable(volume_io_test sources/volume_io_test.cpp)
  target_link_libraries(volume_io_test PRIVATE mobagen::volume_io)
endif()
