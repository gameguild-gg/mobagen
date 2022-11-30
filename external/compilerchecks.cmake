#CHECK MINGW
IF(NOT DEFINED MINGW)
    SET(MINGW OFF)
ENDIF()

# check if mingw
IF("${CMAKE_GENERATOR}" MATCHES "(M|m?)in(G|g?)(W|w?)")
    SET(MINGW ON)
    MESSAGE(STATUS "MinGW Detected")
    MESSAGE(STATUS "${CMAKE_GENERATOR}")
ELSE()
    SET(MINGW OFF CACHE BOOL "MINGW")
ENDIF()

# CHECK OR APPLE MACHINE
IF(NOT DEFINED APPLE)
    set(APPLE OFF)
    MESSAGE(STATUS "NOT APPLE MACHINE")
ENDIF()

find_program(LSB_RELEASE_EXEC lsb_release)
if(LSB_RELEASE_EXEC)
    execute_process(COMMAND ${LSB_RELEASE_EXEC} -is
            OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            )
    IF(LSB_RELEASE_ID_SHORT)
        message(STATUS "ubuntu detected")
        SET(UBUNTU ON)
    ENDIF()
ENDIF()

# todo: make this more general approach
#set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "CMAKE_POSITION_INDEPENDENT_CODE")
#IF(DEFINED UBUNTU)
#    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
#    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
#ENDIF()

MESSAGE(STATUS "MSYS=${MSYS}")
MESSAGE(STATUS "CYGWIN=${CYGWIN}")
MESSAGE(STATUS "MINGW=${MINGW}")
MESSAGE(STATUS "WIN32=${WIN32}")
MESSAGE(STATUS "MSVC=${MSVC}")
MESSAGE(STATUS "UBUNTU=${UBUNTU}")

message(STATUS "Compiler version: ${CMAKE_CXX_COMPILER_VERSION}")

# Option to override which C++ standard to use
set(CXX_STANDARD_TARGET DETECT CACHE STRING "Override the default CXX_STANDARD to compile with.")
set_property(CACHE CXX_STANDARD_TARGET PROPERTY STRINGS DETECT 20 23 26)

message(STATUS "CMAKE_CXX_COMPILE_FEATURES: ${CMAKE_CXX_COMPILE_FEATURES}")
message(STATUS "CMAKE_C_COMPILE_FEATURES: ${CMAKE_C_COMPILE_FEATURES}")

# todo: improve the checks
# Decide on the standard to use
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



# Option to override which C standard to use
#set(C_STANDARD_TARGET DETECT CACHE STRING "Override the default C_STANDARD to compile with.")
#set_property(CACHE C_STANDARD_TARGET PROPERTY STRINGS DETECT 11 17 23)
#
## Decide on the standard to use
#if(C_STANDARD_TARGET STREQUAL "11")
#    if("c_std_11" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        message(STATUS "Using C11 standard")
#        set(CMAKE_C_STANDARD 11)
#    else()
#        message(FATAL_ERROR "Requested C_STANDARD_TARGET \"11\" not supported by provided C compiler")
#    endif()
#elseif(CXX_STANDARD_TARGET STREQUAL "17")
#    if("c_std_17" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        message(STATUS "Using C17 standard")
#        set(CMAKE_C_STANDARD 17)
#    else()
#        message(FATAL_ERROR "Requested C_STANDARD_TARGET \"17\" not supported by provided C compiler")
#    endif()
#elseif(C_STANDARD_TARGET STREQUAL "23")
#    if("c_std_23" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        message(STATUS "Using C23 standard")
#        set(CMAKE_C_STANDARD 23)
#    else()
#        message(FATAL_ERROR "Requested C_STANDARD_TARGET \"23\" not supported by provided C compiler")
#    endif()
#else()
#    if("c_std_23" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        set(CMAKE_C_STANDARD 23)
#        message(STATUS "Detected support for C23 standard")
#    elseif("c_std_17" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        set(CMAKE_C_STANDARD 17)
#        message(STATUS "Detected support for C17 standard")
#    elseif("c_std_11" IN_LIST CMAKE_C_COMPILE_FEATURES)
#        set(CMAKE_C_STANDARD 11)
#        message(STATUS "Detected support for C11 standard")
#    else()
#        message(WARNING "Cannot detect C_STANDARD of C11 or newer.")
#    endif()
#endif()
#set(CMAKE_C_STANDARD_REQUIRED ON)