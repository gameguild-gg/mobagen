add_subdirectory(volume)
add_subdirectory(volume_io)

if(BUILD_OPENGL_RENDERER_MODULE)
    add_subdirectory(opengl_renderer)
endif()