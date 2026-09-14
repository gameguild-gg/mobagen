add_subdirectory(headless)

if(MOBAGEN_BUILD_MANIFEST_TOOLS AND NOT EMSCRIPTEN)
  add_subdirectory(native)
endif()

if(MOBAGEN_BUILD_RUNTIME)
  add_subdirectory(graphics)
endif()
