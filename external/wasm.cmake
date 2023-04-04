string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
# set (WAMR_BUILD_PLATFORM "linux")
# set (WAMR_BUILD_TARGET "X86_64")
set(WAMR_BUILD_INTERP 1)
# set (WAMR_BUILD_FAST_INTERP 1) set (WAMR_BUILD_AOT 1) set (WAMR_BUILD_LIBC_BUILTIN 1) set
# (WAMR_BUILD_LIBC_WASI 1) set (WAMR_BUILD_SIMD 1) set (WAMR_ROOT_DIR path/to/wamr/root)

string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME wasm-micro-runtime
  GITHUB_REPOSITORY bytecodealliance/wasm-micro-runtime RECURSIVE YES
  GIT_TAG WAMR-1.1.2
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAwasm "${AFTER} - ${BEFORE}")
message(STATUS "wasm TIME: ${DELTAwasm}s")
if(wasm-micro-runtime_ADDED)
  include_directories(${wasm-micro-runtime_SOURCE_DIR}/include)
  include(${wasm-micro-runtime_SOURCE_DIR}/build-scripts/runtime_lib.cmake)
  # add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})
else()
  message(FATAL_ERROR "wasm not configured correctly")
endif()
