add_library(mobagen_opengl_renderer STATIC
    sources/shader_program.cpp
    sources/vertex_buffer.cpp
    sources/vertex_array.cpp
    sources/texture.cpp
    sources/texture3d.cpp
    sources/framebuffer.cpp
    sources/renderer.cpp
    sources/engine_c.cpp
)
add_library(mobagen::opengl_renderer ALIAS mobagen_opengl_renderer)
target_include_directories(mobagen_opengl_renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sources)
target_compile_features(mobagen_opengl_renderer PUBLIC cxx_std_20)
target_link_libraries(mobagen_opengl_renderer PUBLIC glm)

if(NOT EMSCRIPTEN)
    include(${PROJECT_SOURCE_DIR}/external/glew.cmake)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(mobagen_opengl_renderer PUBLIC GLEW_STATIC)
    target_link_libraries(mobagen_opengl_renderer PUBLIC OpenGL::GL libglew_static)
endif()