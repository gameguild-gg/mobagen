include_guard(GLOBAL)

function(mobagen_enable_coverage)
  if(NOT ENABLE_TEST_COVERAGE)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target IN LISTS ARGN)
      if(NOT TARGET ${target})
        message(FATAL_ERROR "Cannot enable coverage for missing target '${target}'")
      endif()
      target_compile_options(${target} PRIVATE -O0 -g --coverage)
      target_link_options(${target} PRIVATE --coverage)
    endforeach()
  else()
    message(
      STATUS
        "Coverage instrumentation is unavailable for ${CMAKE_CXX_COMPILER_ID}; tests remain enabled"
    )
  endif()
endfunction()
