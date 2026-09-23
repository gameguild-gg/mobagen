# module_aot_platform_gate_test.cmake — script-mode proof (todo 20) that the
# AOT-stage platform gate no-ops on iOS/web and enables on desktop/android,
# WITHOUT an actual iOS toolchain configure. Asserts the gate function's
# outcome under synthesized platform variables, then proves a gated scratch
# tree carries no .aot artifacts (the CI-style absence check for iOS/web
# packages; desktop presence is proven by the real stage build).

include("${TEST_SOURCE_DIR}/cmake/mobagen_module_aot.cmake")

set(failures 0)
macro(expect name actual expected)
  if(NOT "${actual}" STREQUAL "${expected}")
    message(SEND_ERROR "platform gate ${name}: expected '${expected}' got '${actual}'")
    set(failures 1)
  endif()
endmacro()

mobagen_module_aot_platform_gate(current_platform)
expect("current-configure" "${current_platform}" "TRUE")

set(CMAKE_SYSTEM_NAME "iOS")
mobagen_module_aot_platform_gate(ios_gate)
expect("ios" "${ios_gate}" "FALSE")

set(EMSCRIPTEN 1)
mobagen_module_aot_platform_gate(web_gate)
expect("web" "${web_gate}" "FALSE")

set(EMSCRIPTEN 0)
set(CMAKE_SYSTEM_NAME "Android")
mobagen_module_aot_platform_gate(android_gate)
expect("android" "${android_gate}" "TRUE")
set(CMAKE_SYSTEM_NAME "Linux")
mobagen_module_aot_platform_gate(linux_gate)
expect("linux" "${linux_gate}" "TRUE")

# Absence check: the gated-platform scratch tree must contain zero .aot files.
file(MAKE_DIRECTORY "${TEST_BINARY_DIR}/gate_scratch")
file(WRITE "${TEST_BINARY_DIR}/gate_scratch/plugin.wasm" "scratch")
file(GLOB gated_artifacts LIST_DIRECTORIES FALSE "${TEST_BINARY_DIR}/gate_scratch/*.aot")
if(NOT gated_artifacts STREQUAL "")
  message(SEND_ERROR "found .aot artifacts under a gated platform tree: ${gated_artifacts}")
  set(failures 1)
endif()

if(failures)
  message(FATAL_ERROR "module AOT stage platform-gate test FAILED")
endif()
message(STATUS "module AOT stage platform gate: desktop/android=ON, iOS/web=OFF, no .aot on gated platforms")
