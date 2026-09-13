option(MOBAGEN_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer on Mobagen test targets" OFF
)

if(MOBAGEN_ENABLE_SANITIZERS)
  if(EMSCRIPTEN)
    message(FATAL_ERROR "MOBAGEN_ENABLE_SANITIZERS is not supported by the Web build")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    message(
      FATAL_ERROR
        "MOBAGEN_ENABLE_SANITIZERS requires GCC or Clang; detected ${CMAKE_CXX_COMPILER_ID}"
    )
  endif()
  if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    message(FATAL_ERROR "MOBAGEN_ENABLE_SANITIZERS requires the GNU-style GCC or Clang frontend")
  endif()
  if(USE_SANITIZER)
    message(
      FATAL_ERROR
        "MOBAGEN_ENABLE_SANITIZERS cannot be combined with the legacy USE_SANITIZER option"
    )
  endif()
endif()

function(mobagen_enable_sanitizers)
  if(NOT MOBAGEN_ENABLE_SANITIZERS)
    return()
  endif()

  set(_compile_options -fsanitize=address,undefined -fno-omit-frame-pointer
                       -fno-sanitize-recover=all
  )
  set(_link_options -fsanitize=address,undefined -fno-sanitize-recover=all)

  foreach(_target_name ${ARGN})
    if(NOT TARGET ${_target_name})
      message(FATAL_ERROR "Cannot enable sanitizers for missing target '${_target_name}'")
    endif()

    get_target_property(_aliased_target ${_target_name} ALIASED_TARGET)
    if(_aliased_target)
      set(_target ${_aliased_target})
    else()
      set(_target ${_target_name})
    endif()

    get_target_property(_target_type ${_target} TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${_target} INTERFACE ${_compile_options})
      target_link_options(${_target} INTERFACE ${_link_options})
    else()
      target_compile_options(${_target} PRIVATE ${_compile_options})
      if(_target_type STREQUAL "EXECUTABLE"
         OR _target_type STREQUAL "SHARED_LIBRARY"
         OR _target_type STREQUAL "MODULE_LIBRARY"
      )
        target_link_options(${_target} PRIVATE ${_link_options})
      endif()
    endif()
  endforeach()
endfunction()
