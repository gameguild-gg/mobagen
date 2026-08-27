set(DICOM_VIEWER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sources)
set(DICOM_VIEWER_ASSET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set(DICOM_VIEWER_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(DICOM_VIEWER_HTML_DIR ${CMAKE_CURRENT_SOURCE_DIR}/htmls)

set(DICOM_VIEWER_WGSL_SHADERS raygen.wgsl blit.wgsl histogram.wgsl)
set(DICOM_VIEWER_GEN_DIR ${CMAKE_BINARY_DIR}/generated/dicom_viewer)
set(DICOM_VIEWER_GEN_HEADER ${DICOM_VIEWER_GEN_DIR}/embedded_shaders.h)

set(_gen
    "#pragma once\n// AUTO-GENERATED from apps/dicom_viewer/shaders/*.wgsl. Edit those, not this.\nnamespace shaders {\n\n"
)
foreach(_f ${DICOM_VIEWER_WGSL_SHADERS})
  file(READ ${DICOM_VIEWER_SHADER_DIR}/${_f} _content)
  string(MAKE_C_IDENTIFIER ${_f} _id)
  string(TOUPPER ${_id} _id)
  string(APPEND _gen "inline constexpr const char* ${_id} = R\"WGSL(\n${_content})WGSL\";\n\n")
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${DICOM_VIEWER_SHADER_DIR}/${_f}
  )
endforeach()
string(APPEND _gen "}  // namespace shaders\n")
file(WRITE ${DICOM_VIEWER_GEN_HEADER} "${_gen}")

add_executable(dicom_renderer ${DICOM_VIEWER_SOURCE_DIR}/main.cpp)
target_include_directories(dicom_renderer PRIVATE ${DICOM_VIEWER_GEN_DIR})
target_link_libraries(
  dicom_renderer PRIVATE glm mobagen::core_camera mobagen::core_render mobagen::volume
)

include(${PROJECT_SOURCE_DIR}/external/sdl.cmake)
target_link_libraries(dicom_renderer PRIVATE SDL3::SDL3-static)

if(USE_GDCM)
  target_compile_definitions(dicom_renderer PRIVATE HAVE_GDCM)
  target_link_libraries(dicom_renderer PRIVATE mobagen::volume_io)
  if(NOT EMSCRIPTEN)
    target_compile_definitions(
      dicom_renderer PRIVATE MOBAGEN_DICOM_PATH="${DICOM_VIEWER_ASSET_DIR}/dicom"
    )
  endif()
endif()
if(USE_ITK)
  target_compile_definitions(dicom_renderer PRIVATE HAVE_ITK)
endif()
if(USE_VTK)
  target_compile_definitions(dicom_renderer PRIVATE HAVE_VTK)
endif()

set(VOLUME_RAW ${DICOM_VIEWER_ASSET_DIR}/volume.raw)
set(VOLUME_MVOL ${DICOM_VIEWER_ASSET_DIR}/volume.mvol)

# WebGPU/Dawn only — unconditional
target_compile_definitions(dicom_renderer PRIVATE USE_WEBGPU)
target_link_libraries(dicom_renderer PRIVATE IMGUI dawn::webgpu)
message(STATUS "Renderer: WebGPU (G3) - Dawn + ImGui")

if(EMSCRIPTEN)
  set(SHELL_FILE ${DICOM_VIEWER_HTML_DIR}/shell_dawn.html)
  message(STATUS "Shell file: ${SHELL_FILE}")
  set_property(
    TARGET dicom_renderer
    APPEND
    PROPERTY LINK_DEPENDS "${SHELL_FILE}"
  )
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${SHELL_FILE}"
  )
  target_compile_options(dicom_renderer PRIVATE -O3)
  target_link_options(
    dicom_renderer
    PRIVATE
    -O3
    -sUSE_SDL=0
    -sASSERTIONS=1
    -sALLOW_MEMORY_GROWTH=1
    --shell-file
    ${SHELL_FILE}
  )
  if(EXISTS ${VOLUME_MVOL})
    target_link_options(dicom_renderer PRIVATE "SHELL:--preload-file ${VOLUME_MVOL}@/volume.mvol")
  else()
    message(
      WARNING "${VOLUME_MVOL} not found; web WebGPU build will use the phantom. Run: make assets"
    )
  endif()
  set_target_properties(dicom_renderer PROPERTIES SUFFIX ".html")
else()
  target_compile_definitions(
    dicom_renderer PRIVATE VOLUME_PATH="${VOLUME_RAW}" MOBAGEN_MVOL_PATH="${VOLUME_MVOL}"
  )
endif()
