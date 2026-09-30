option(MOBAGEN_BUILD_MODULE_TOOLS "Build the native Mobagen module manifest command-line tools" ON)

add_library(mobagen_module_extraction STATIC extraction_cli.cpp extraction_cli.hpp)
add_library(mobagen::module_extraction ALIAS mobagen_module_extraction)
target_compile_features(mobagen_module_extraction PUBLIC cxx_std_20)
target_include_directories(mobagen_module_extraction PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mobagen_module_extraction PUBLIC mobagen::module_manifest mobagen::plugin_sdk)

add_executable(MobagenModuleManifest main.cpp)
target_compile_features(MobagenModuleManifest PRIVATE cxx_std_20)
target_link_libraries(MobagenModuleManifest PRIVATE mobagen::module_extraction)

# Reference guest for the signature-extraction golden test. The wasm binaries are
# never committed: they are compiled on demand with the emsdk emcc when present
# and the tests skip with an explicit message otherwise.
if(BUILD_TESTING)
  find_program(MOBAGEN_EMCC_EXECUTABLE NAMES emcc HINTS "${CMAKE_SOURCE_DIR}/external/emsdk/upstream/emscripten")
  if(MOBAGEN_EMCC_EXECUTABLE)
    set(MOBAGEN_MODULE_REFERENCE_WASM ${CMAKE_CURRENT_BINARY_DIR}/reference_guest/plugin.wasm)
    set(MOBAGEN_MODULE_ENTRYLESS_WASM ${CMAKE_CURRENT_BINARY_DIR}/entryless_guest/plugin.wasm)
    add_custom_command(
      OUTPUT ${MOBAGEN_MODULE_REFERENCE_WASM}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/reference_guest
      COMMAND
        ${MOBAGEN_EMCC_EXECUTABLE} -Os -sSTANDALONE_WASM=1 -Wl,--no-entry -Wl,--export-memory
        -Wl,--export=mobagen_module_exports_v1 -Wl,--export=mobagen_module_entry_v1
        -I${CMAKE_SOURCE_DIR}/sdk/include -o ${MOBAGEN_MODULE_REFERENCE_WASM}
        ${CMAKE_CURRENT_SOURCE_DIR}/reference_guest.c
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/reference_guest.c ${CMAKE_SOURCE_DIR}/sdk/include/mobagen/module/module_abi.h
      COMMENT "compiling reference guest wasm"
      VERBATIM
    )
    add_custom_command(
      OUTPUT ${MOBAGEN_MODULE_ENTRYLESS_WASM}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/entryless_guest
      COMMAND
        ${MOBAGEN_EMCC_EXECUTABLE} -Os -sSTANDALONE_WASM=1 -Wl,--no-entry -Wl,--export-memory
        -Wl,--export=entryless_guest_value -o ${MOBAGEN_MODULE_ENTRYLESS_WASM}
        ${CMAKE_CURRENT_SOURCE_DIR}/entryless_guest.c
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/entryless_guest.c
      COMMENT "compiling entry-less guest wasm"
      VERBATIM
    )
    add_custom_target(mobagen_module_reference_guests DEPENDS ${MOBAGEN_MODULE_REFERENCE_WASM} ${MOBAGEN_MODULE_ENTRYLESS_WASM})

    # wamrc AOT stage proof (todo 20): reference guest wasm → plugin.aot
    # (host arch + aarch64 cross) + module.manifest stamped with the wamrc
    # toolchain version. Desktop/android only — the helper no-ops on iOS/web.
    # T21 applies the same helper to modules/asset_store.
    include(${CMAKE_SOURCE_DIR}/cmake/mobagen_module_aot.cmake)
    mobagen_add_module_aot_stage(mobagen_module_reference_guests
      WASM ${MOBAGEN_MODULE_REFERENCE_WASM}
      MANIFEST_TOOL $<TARGET_FILE:MobagenModuleManifest>
    )

    # Platform-gate check (todo 20): the helper must no-op on iOS/web with a
    # STATUS message and never produce .aot there. CMake script mode — no iOS
    # toolchain configure needed. (enable_testing: root calls it only AFTER
    # this subdirectory, which would discard the add_test; the name avoids
    # spaces/colons — ctest mis-parses those in add_test script commands.)
    enable_testing()
    add_test(
      NAME foundation::module-aot-stage-platform-gate
      COMMAND ${CMAKE_COMMAND}
        -DTEST_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/module_aot_platform_gate_test.cmake
    )
  endif()
endif()
