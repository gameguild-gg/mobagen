# Helper script invoked at build time (via add_custom_target). Creates a monolithic libwebgpu_dawn.a
# containing: - Dawn native objects (dawn_native_objects.a + webgpu_dawn_objects.a) - All
# Dawn/Tint/Abseil/SPIRV component static libraries This is needed because CMake's Xcode generator
# does not propagate PRIVATE static-lib dependencies to final executable OTHER_LDFLAGS.
#
# Variables expected (passed via -D): BUILD_DIR       = CMAKE_BINARY_DIR OUTPUT_DIR      =
# CMAKE_ARCHIVE_OUTPUT_DIRECTORY  (without config suffix) CONFIG          = current build
# configuration (e.g. MinSizeRel) PLATFORM        = iphoneos or iphonesimulator

set(_OBJ1
    "${BUILD_DIR}/build/webgpu_dawn_objects.build/${CONFIG}-${PLATFORM}/libwebgpu_dawn_objects.a"
)
set(_OBJ2
    "${BUILD_DIR}/build/dawn_native_objects.build/${CONFIG}-${PLATFORM}/libdawn_native_objects.a"
)
set(_OUT "${OUTPUT_DIR}/${CONFIG}/libwebgpu_dawn.a")

# Skip silently when the object archives for this configuration don't exist yet (e.g. when building
# MinSizeRel the Release-iphoneos archives are absent).
foreach(_f "${_OBJ1}" "${_OBJ2}")
  if(NOT EXISTS "${_f}")
    message(STATUS "ios_merge_webgpu_dawn: skipping ${CONFIG} — archive not found: ${_f}")
    return()
  endif()
endforeach()

# Collect all Dawn / Tint / Abseil / SPIRV component static libs. These are not included in
# OTHER_LDFLAGS by the Xcode generator's transitive dependency traversal, so we bake them all into
# the monolithic archive.
file(GLOB _COMPONENT_LIBS "${OUTPUT_DIR}/${CONFIG}/libdawn_*.a"
     "${OUTPUT_DIR}/${CONFIG}/libtint_*.a" "${OUTPUT_DIR}/${CONFIG}/libabsl_*.a"
     "${OUTPUT_DIR}/${CONFIG}/libspirv_*.a"
)
# Remove the output file from the input list (in case it exists from a previous run).
list(REMOVE_ITEM _COMPONENT_LIBS "${_OUT}")

file(MAKE_DIRECTORY "${OUTPUT_DIR}/${CONFIG}")

# Detect architectures present in the input archives.
execute_process(
  COMMAND xcrun lipo -info "${_OBJ1}"
  OUTPUT_VARIABLE _lipo_out
  ERROR_QUIET
)
set(_ARCH_LIST)
if(_lipo_out MATCHES "arm64")
  list(APPEND _ARCH_LIST arm64)
endif()
if(_lipo_out MATCHES "x86_64")
  list(APPEND _ARCH_LIST x86_64)
endif()
if(NOT _ARCH_LIST)
  message(FATAL_ERROR "ios_merge_webgpu_dawn: could not detect any architecture in ${_OBJ1}")
endif()

set(_TEMP_DIR "${OUTPUT_DIR}/${CONFIG}/_dawn_merge_tmp")
file(MAKE_DIRECTORY "${_TEMP_DIR}")

set(_ARCH_OUTPUTS)
foreach(_ARCH IN LISTS _ARCH_LIST)
  set(_ARCH_FILE "${_TEMP_DIR}/libwebgpu_dawn_${_ARCH}.a")
  execute_process(
    COMMAND xcrun libtool -static -arch_only ${_ARCH} -o "${_ARCH_FILE}" "${_OBJ1}" "${_OBJ2}"
            ${_COMPONENT_LIBS} RESULT_VARIABLE _rc
  )
  if(_rc)
    message(FATAL_ERROR "ios_merge_webgpu_dawn: libtool for ${_ARCH} failed (exit ${_rc})")
  endif()
  list(APPEND _ARCH_OUTPUTS "${_ARCH_FILE}")
endforeach()

if(_ARCH_OUTPUTS)
  list(LENGTH _ARCH_OUTPUTS _arch_count)
  if(_arch_count GREATER 1)
    # Multiple archs → lipo together into a universal binary.
    execute_process(COMMAND xcrun lipo -create -o "${_OUT}" ${_ARCH_OUTPUTS} RESULT_VARIABLE _rc)
  else()
    # Single arch → just rename.
    file(RENAME "${_ARCH_OUTPUTS}" "${_OUT}")
  endif()
  if(_rc)
    message(FATAL_ERROR "ios_merge_webgpu_dawn: lipo failed (exit ${_rc})")
  endif()
endif()

# Clean up temp dir.
file(REMOVE_RECURSE "${_TEMP_DIR}")

message(STATUS "Created monolithic ${_OUT} (${_ARCH_LIST})")
