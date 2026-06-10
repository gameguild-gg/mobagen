if(BUILD_RENDERER)
    add_subdirectory(dicom_viewer)
endif()

if(BUILD_CORE_EXAMPLES)
    add_subdirectory(core_demos)
endif()

if(BUILD_VOLUME_DEMOS AND NOT EMSCRIPTEN)
    add_subdirectory(volume_demos)
endif()

if(BUILD_PROTOTYPES AND NOT EMSCRIPTEN)
    add_subdirectory(dawn_probe)
    add_subdirectory(fibers_prototype)
endif()