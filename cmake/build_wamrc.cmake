# build_wamrc.cmake — build wamrc FROM THE PINNED WAMR SOURCE (todo 20).
#
# Script mode:
#   cmake -DMOBAGEN_WAMRC_SOURCE_DIR=<wasm-micro-runtime checkout> \
#         -DMOBAGEN_WAMRC_OUTPUT_DIR=<install prefix> \
#         [-DMOBAGEN_WAMRC_LLVM_PREFIX=<LLVM install with LLVMConfig.cmake>] \
#         -P cmake/build_wamrc.cmake
#
# wamrc needs LLVM: wamr-compiler/CMakeLists.txt requires a prebuilt LLVM at
# <source>/core/deps/llvm/build unless WAMR_BUILD_WITH_CUSTOM_LLVM=1 provides
# LLVMConfig.cmake via CMAKE_PREFIX_PATH. Building LLVM itself is out of scope
# here (WAMR ships build_llvm.sh in wamr-compiler/); this script wires the
# compiler build once LLVM exists and installs the binary into the output dir.
#
# After running, configure mobagen with:
#   -DMOBAGEN_WAMRC_EXECUTABLE=<output dir>/wamrc
#
# The pinned source is external/wasm.cmake's CPM checkout
# (external/wasm-micro-runtime/<sha>/); NEVER a system wamrc.

if(NOT DEFINED MOBAGEN_WAMRC_SOURCE_DIR)
  message(FATAL_ERROR "MOBAGEN_WAMRC_SOURCE_DIR must point at the pinned wasm-micro-runtime checkout")
endif()
if(NOT DEFINED MOBAGEN_WAMRC_OUTPUT_DIR)
  message(FATAL_ERROR "MOBAGEN_WAMRC_OUTPUT_DIR must name the install dir for the wamrc binary")
endif()
if(NOT EXISTS "${MOBAGEN_WAMRC_SOURCE_DIR}/wamr-compiler/CMakeLists.txt")
  message(FATAL_ERROR "MOBAGEN_WAMRC_SOURCE_DIR='${MOBAGEN_WAMRC_SOURCE_DIR}' has no wamr-compiler/")
endif()

set(_build "${MOBAGEN_WAMRC_OUTPUT_DIR}/build")
file(MAKE_DIRECTORY "${_build}")

set(_extra_args "")
if(MOBAGEN_WAMRC_LLVM_PREFIX)
  list(APPEND _extra_args
    "-DWAMR_BUILD_WITH_CUSTOM_LLVM=1"
    "-DCMAKE_PREFIX_PATH=${MOBAGEN_WAMRC_LLVM_PREFIX}")
endif()

execute_process(COMMAND
  "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" -S "${MOBAGEN_WAMRC_SOURCE_DIR}/wamr-compiler" -B "${_build}"
  -DCMAKE_BUILD_TYPE=Release ${_extra_args}
  RESULT_VARIABLE _configure_result)
if(NOT _configure_result EQUAL 0)
  message(FATAL_ERROR "wamrc source build configure failed — if LLVM was the complaint, build it first with wamr-compiler/build_llvm.sh and pass MOBAGEN_WAMRC_LLVM_PREFIX")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config Release --target wamrc
  RESULT_VARIABLE _build_result)
if(NOT _build_result EQUAL 0)
  message(FATAL_ERROR "wamrc source build failed")
endif()

# Locate the built binary (single- and multi-config trees).
set(_candidates "${_build}/wamrc" "${_build}/Release/wamrc")
set(_binary "")
foreach(_candidate IN LISTS _candidates)
  if(EXISTS "${_candidate}")
    set(_binary "${_candidate}")
    break()
  endif()
endforeach()
if(NOT _binary)
  message(FATAL_ERROR "wamrc built but the binary was not found under ${_build}")
endif()

file(COPY "${_binary}" DESTINATION "${MOBAGEN_WAMRC_OUTPUT_DIR}")
execute_process(COMMAND "${MOBAGEN_WAMRC_OUTPUT_DIR}/wamrc" --version
  OUTPUT_VARIABLE _version OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _version_result)
if(NOT _version_result EQUAL 0)
  message(FATAL_ERROR "installed wamrc failed --version")
endif()
message(STATUS "wamrc source build installed: ${MOBAGEN_WAMRC_OUTPUT_DIR}/wamrc (${_version})")
