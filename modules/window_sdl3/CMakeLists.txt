if(EMSCRIPTEN
   OR ANDROID
   OR IOS
   OR NOT MOBAGEN_BUILD_RUNTIME
   OR NOT TARGET mobagen::core_app
)
  return()
endif()

if(ANDROID)
  set(_MOBAGEN_WINDOW_SDL_TARGET SDL3::SDL3-shared)
else()
  set(_MOBAGEN_WINDOW_SDL_TARGET SDL3::SDL3-static)
endif()

add_library(MobagenWindowSdl3Plugin MODULE window_sdl3_plugin.cpp)
target_compile_features(MobagenWindowSdl3Plugin PRIVATE cxx_std_20)
target_compile_definitions(MobagenWindowSdl3Plugin PRIVATE MOBAGEN_PLUGIN_BUILD)
target_link_libraries(
  MobagenWindowSdl3Plugin PRIVATE mobagen::plugin_sdk ${_MOBAGEN_WINDOW_SDL_TARGET}
)
set_target_properties(MobagenWindowSdl3Plugin PROPERTIES PREFIX "")
