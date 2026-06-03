# ============================================================================
# Dear ImGui (docking branch) with SDL3 + WebGPU backends
#
# Backends:
#   * imgui_impl_sdl3      (platform / events)
#   * imgui_impl_wgpu      (renderer; Dawn flavor on both native and web)
#
# Requires: SDL3::SDL3-static (external/sdl.cmake)
#           dawn::webgpu      (external/dawn.cmake)
# ============================================================================

string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME IMGUI
  GIT_TAG v1.92.8-docking
  GITHUB_REPOSITORY ocornut/imgui
)

if(IMGUI_ADDED)
  add_library(IMGUI STATIC
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
  )

  target_include_directories(IMGUI
    PUBLIC
      ${IMGUI_SOURCE_DIR}
      ${IMGUI_SOURCE_DIR}/backends
  )

  # imgui_impl_wgpu has multiple flavors. emdawnwebgpu exposes the same C++
  # API as native Dawn (`<webgpu/webgpu_cpp.h>`), so the Dawn flavor is the
  # correct choice on BOTH native and Emscripten/web.
  target_compile_definitions(IMGUI PUBLIC
    IMGUI_IMPL_WEBGPU_BACKEND_DAWN
  )

  target_link_libraries(IMGUI
    PUBLIC
      SDL3::SDL3-static
      dawn::webgpu
      ${CMAKE_DL_LIBS}
  )

  # On Apple, imgui_impl_wgpu.cpp uses Cocoa (CAMetalLayer) and must be
  # compiled as Objective-C++.
  if(APPLE AND NOT EMSCRIPTEN)
    set_source_files_properties(
      ${IMGUI_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
      PROPERTIES
        COMPILE_FLAGS "-x objective-c++ -fno-objc-arc"
    )
    target_link_libraries(IMGUI PUBLIC
      "-framework Cocoa"
      "-framework QuartzCore"
      "-framework Metal"
    )
  endif()
endif()
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAIMGUI "${AFTER} - ${BEFORE}")
message(STATUS "IMGUI TIME: ${DELTAIMGUI}s")
