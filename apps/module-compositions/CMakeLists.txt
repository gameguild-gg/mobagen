add_subdirectory(headless)

if(MOBAGEN_BUILD_MANIFEST_TOOLS)
  add_subdirectory(portable)
  if(NOT EMSCRIPTEN)
    add_subdirectory(native)
  endif()
endif()

if(MOBAGEN_BUILD_RUNTIME)
  add_subdirectory(graphics)
endif()
