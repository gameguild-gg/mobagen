cmake_minimum_required(VERSION 3.16.3)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/patches/dawn-no-environment-dump.cmake")

set(test_root "${CMAKE_CURRENT_BINARY_DIR}/build-cmake-tests/dawn-patch")
set(dawn_root "${test_root}/dawn")
set(dawn_patch_target "${dawn_root}/third_party/CopyWindowsSDKDLL.cmake")
file(MAKE_DIRECTORY "${dawn_root}/third_party")

set(vulnerable_source
    [=[prefix
    message(STATUS "Display environment variables:")
    execute_process(COMMAND ${CMAKE_COMMAND} -E environment COMMAND_ECHO STDOUT)

suffix
]=]
)
file(WRITE "${dawn_patch_target}" "${vulnerable_source}")

mobagen_patch_dawn_environment_dump("${dawn_root}")

file(READ "${dawn_patch_target}" patched_source)
string(REPLACE "\r\n" "\n" patched_source "${patched_source}")
if(NOT
   patched_source
   STREQUAL
   "prefix\n    # Mobagen: upstream environment dump removed to protect configure-time secrets.\nsuffix\n"
)
  message(FATAL_ERROR "Dawn patch changed content outside the audited environment dump block")
endif()

mobagen_patch_dawn_environment_dump("${dawn_root}")
file(READ "${dawn_patch_target}" patched_twice_source)
string(REPLACE "\r\n" "\n" patched_twice_source "${patched_twice_source}")
if(NOT patched_twice_source STREQUAL patched_source)
  message(FATAL_ERROR "Dawn patch is not idempotent")
endif()

set(unknown_root "${test_root}/unknown-dawn")
set(unknown_patch_target "${unknown_root}/third_party/CopyWindowsSDKDLL.cmake")
file(MAKE_DIRECTORY "${unknown_root}/third_party")
file(WRITE "${unknown_patch_target}" "upstream content changed\n")

set(reject_driver "${test_root}/reject-unknown.cmake")
file(WRITE "${reject_driver}"
     "include(\"${CMAKE_CURRENT_LIST_DIR}/../../cmake/patches/dawn-no-environment-dump.cmake\")\n"
     "mobagen_patch_dawn_environment_dump(\"${unknown_root}\")\n"
)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -P "${reject_driver}"
  RESULT_VARIABLE reject_result
  OUTPUT_VARIABLE reject_output
  ERROR_VARIABLE reject_error
)
if(reject_result EQUAL 0)
  message(FATAL_ERROR "Dawn patch accepted unknown upstream content")
endif()
string(FIND "${reject_output}${reject_error}" "does not match the audited source"
            rejection_position
)
if(rejection_position EQUAL -1)
  message(FATAL_ERROR "Dawn patch rejected unknown content without the expected diagnostic")
endif()
