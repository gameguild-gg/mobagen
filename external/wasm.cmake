string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME wasm-micro-runtime
  GITHUB_REPOSITORY bytecodealliance/wasm-micro-runtime
  GIT_TAG WAMR-2.4.5
  GIT_SHALLOW TRUE
  DOWNLOAD_ONLY YES
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAwasm "${AFTER} - ${BEFORE}")
message(STATUS "wasm TIME: ${DELTAwasm}s")
if(NOT EXISTS "${wasm-micro-runtime_SOURCE_DIR}/build-scripts/runtime_lib.cmake")
  message(FATAL_ERROR "WAMR ${wasm-micro-runtime_VERSION} did not provide runtime_lib.cmake")
endif()
