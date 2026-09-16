if(EMSCRIPTEN
   OR ANDROID
   OR IOS
   OR NOT MOBAGEN_BUILD_RUNTIME
   OR NOT TARGET mobagen::core_app
)
  return()
endif()

add_library(MobagenRenderWebgpuPlugin MODULE render_webgpu_plugin.cpp)
target_compile_features(MobagenRenderWebgpuPlugin PRIVATE cxx_std_20)
target_compile_definitions(MobagenRenderWebgpuPlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
target_link_libraries(MobagenRenderWebgpuPlugin PRIVATE mobagen::core_app mobagen::plugin_sdk)
set_target_properties(MobagenRenderWebgpuPlugin PROPERTIES PREFIX "")
