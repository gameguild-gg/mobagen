# CHECK MINGW
if(NOT DEFINED MINGW)
  set(MINGW OFF)
endif()

# check if mingw (only meaningful on Windows; CMake also auto-sets the MINGW variable when using a
# MinGW toolchain). The previous regex (M|m?)in(G|g?)(W|w?) was buggy: with all groups optional it
# effectively matched any generator containing "in" (e.g. "Ninja"), so MINGW was wrongly turned ON
# on macOS.
if(WIN32 AND "${CMAKE_GENERATOR}" MATCHES "[Mm][Ii][Nn][Gg][Ww]")
  set(MINGW ON)
  message(STATUS "MinGW Detected")
  message(STATUS "${CMAKE_GENERATOR}")
else()
  set(MINGW
      OFF
      CACHE BOOL "MINGW"
  )
endif()

# Windows builds must use the Visual Studio toolchain (MSVC or ClangCL). Dawn's D3D11/D3D12
# backends require Windows SDK headers and the MSVC environment; MinGW and other toolchains fail
# with errors like "DXProgrammableCapture.h: No such file or directory". Note: ClangCL also sets
# MSVC=1, so it is accepted. This also catches CLion's bundled MinGW GCC (jetbrains ...\bin\mingw).
if(WIN32 AND NOT MSVC)
  message(
    FATAL_ERROR
      "MoBaGEn on Windows requires the Visual Studio toolchain (MSVC or ClangCL), but this "
      "configure is using: '${CMAKE_CXX_COMPILER}' "
      "(${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}). "
      "Dawn (WebGPU) compiles its D3D backends against the Windows SDK, which MinGW/other "
      "toolchains do not provide (e.g. fatal error: DXProgrammableCapture.h: No such file or "
      "directory). If you are using CLion, switch the toolchain under Settings -> Build, "
      "Execution, Deployment -> Toolchains to 'Visual Studio' (install Visual Studio with the "
      "'Desktop development with C++' workload first if needed), then delete the build directory "
      "(e.g. cmake-build-debug) and reload the CMake project."
  )
endif()

# CHECK OR APPLE MACHINE
if(NOT DEFINED APPLE)
  set(APPLE OFF)
  message(STATUS "NOT APPLE MACHINE")
endif()

find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
  execute_process(
    COMMAND ${LSB_RELEASE_EXEC} -is
    OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(LSB_RELEASE_ID_SHORT)
    message(STATUS "ubuntu detected")
    set(UBUNTU ON)
  endif()
endif()

# todo: make this more general approach set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL
# "CMAKE_POSITION_INDEPENDENT_CODE") IF(DEFINED UBUNTU) SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}
# -fPIC") SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC") ENDIF()

message(STATUS "MSYS=${MSYS}")
message(STATUS "CYGWIN=${CYGWIN}")
message(STATUS "MINGW=${MINGW}")
message(STATUS "WIN32=${WIN32}")
message(STATUS "MSVC=${MSVC}")
message(STATUS "UBUNTU=${UBUNTU}")

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

# Option to override which C++ standard to use
set(CXX_STANDARD_TARGET
    "23"
    CACHE STRING "Override the default CXX_STANDARD to compile with."
)
set_property(CACHE CXX_STANDARD_TARGET PROPERTY STRINGS DETECT 20 23 26)

message(STATUS "CMAKE_CXX_COMPILE_FEATURES: ${CMAKE_CXX_COMPILE_FEATURES}")
message(STATUS "CMAKE_C_COMPILE_FEATURES: ${CMAKE_C_COMPILE_FEATURES}")

# todo: improve the checks Decide on the standard to use
if(CXX_STANDARD_TARGET STREQUAL "20")
  if("cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++20 standard")
    set(CMAKE_CXX_STANDARD 20)
  else()
    message(
      FATAL_ERROR "Requested CXX_STANDARD_TARGET \"20\" not supported by provided C++ compiler"
    )
  endif()
elseif(CXX_STANDARD_TARGET STREQUAL "23")
  if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++23 standard")
    set(CMAKE_CXX_STANDARD 23)
  else()
    message(
      FATAL_ERROR "Requested CXX_STANDARD_TARGET \"23\" not supported by provided C++ compiler"
    )
  endif()
elseif(CXX_STANDARD_TARGET STREQUAL "26")
  if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(STATUS "Using C++26 standard")
    set(CMAKE_CXX_STANDARD 26)
  else()
    message(
      FATAL_ERROR "Requested CXX_STANDARD_TARGET \"26\" not supported by provided C++ compiler"
    )
  endif()
else()
  if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(CMAKE_CXX_STANDARD 26)
    message(STATUS "Detected support for C++26 standard")
  elseif("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(CMAKE_CXX_STANDARD 23)
    message(STATUS "Detected support for C++23 standard")
  elseif("cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(CMAKE_CXX_STANDARD 20)
    message(STATUS "Detected support for C++20 standard")
  else()
    message(FATAL_ERROR "Cannot detect CXX_STANDARD of C++20 or newer.")
  endif()
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to override which C standard to use set(C_STANDARD_TARGET DETECT CACHE STRING "Override the
# default C_STANDARD to compile with.") set_property(CACHE C_STANDARD_TARGET PROPERTY STRINGS DETECT
# 11 17 23)
#
# Decide on the standard to use if(C_STANDARD_TARGET STREQUAL "11") if("c_std_11" IN_LIST
# CMAKE_C_COMPILE_FEATURES) message(STATUS "Using C11 standard") set(CMAKE_C_STANDARD 11) else()
# message(FATAL_ERROR "Requested C_STANDARD_TARGET \"11\" not supported by provided C compiler")
# endif() elseif(CXX_STANDARD_TARGET STREQUAL "17") if("c_std_17" IN_LIST CMAKE_C_COMPILE_FEATURES)
# message(STATUS "Using C17 standard") set(CMAKE_C_STANDARD 17) else() message(FATAL_ERROR
# "Requested C_STANDARD_TARGET \"17\" not supported by provided C compiler") endif()
# elseif(C_STANDARD_TARGET STREQUAL "23") if("c_std_23" IN_LIST CMAKE_C_COMPILE_FEATURES)
# message(STATUS "Using C23 standard") set(CMAKE_C_STANDARD 23) else() message(FATAL_ERROR
# "Requested C_STANDARD_TARGET \"23\" not supported by provided C compiler") endif() else()
# if("c_std_23" IN_LIST CMAKE_C_COMPILE_FEATURES) set(CMAKE_C_STANDARD 23) message(STATUS "Detected
# support for C23 standard") elseif("c_std_17" IN_LIST CMAKE_C_COMPILE_FEATURES)
# set(CMAKE_C_STANDARD 17) message(STATUS "Detected support for C17 standard") elseif("c_std_11"
# IN_LIST CMAKE_C_COMPILE_FEATURES) set(CMAKE_C_STANDARD 11) message(STATUS "Detected support for
# C11 standard") else() message(WARNING "Cannot detect C_STANDARD of C11 or newer.") endif() endif()
# set(CMAKE_C_STANDARD_REQUIRED ON)
