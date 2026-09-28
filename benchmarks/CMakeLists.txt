if(TARGET mobagen::core_ecs
   AND TARGET mobagen::core_jobs
   AND TARGET mobagen::core_scene
   AND TARGET mobagen::core_render
)
  add_executable(MobagenFoundationBenchmark foundation_baseline.cpp)
  target_compile_features(MobagenFoundationBenchmark PRIVATE cxx_std_20)
  target_include_directories(
    MobagenFoundationBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
  )
  target_link_libraries(
    MobagenFoundationBenchmark PRIVATE mobagen::core_ecs mobagen::core_jobs mobagen::core_scene
                                       mobagen::core_render
  )
endif()

add_executable(MobagenModuleDispatchBenchmark module_dispatch.cpp)
target_compile_features(MobagenModuleDispatchBenchmark PRIVATE cxx_std_20)
target_include_directories(
  MobagenModuleDispatchBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
)
target_link_libraries(MobagenModuleDispatchBenchmark PRIVATE mobagen::core_modules)

if(NOT EMSCRIPTEN)
  add_executable(MobagenMemoryAllocGCBenchmark memory_alloc_gc_benchmark.cpp)
  target_compile_features(MobagenMemoryAllocGCBenchmark PRIVATE cxx_std_20)
  target_include_directories(
    MobagenMemoryAllocGCBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
  )
  target_link_libraries(
    MobagenMemoryAllocGCBenchmark PRIVATE mobagen::memory_core mobagen::memory_native_shim
  )
endif()

if(BUILD_TESTING)
  if(TARGET MobagenFoundationBenchmark)
    add_test(NAME benchmark::foundation-smoke COMMAND MobagenFoundationBenchmark --warmup 1
                                                      --samples 2
    )
    set_tests_properties(benchmark::foundation-smoke PROPERTIES LABELS benchmark)
  endif()
  add_test(NAME benchmark::module-dispatch-smoke COMMAND MobagenModuleDispatchBenchmark --warmup 1
                                                         --samples 2
  )
  set_tests_properties(benchmark::module-dispatch-smoke PROPERTIES LABELS benchmark)
  if(TARGET MobagenMemoryAllocGCBenchmark)
    add_test(NAME benchmark::memory-alloc-gc-smoke COMMAND MobagenMemoryAllocGCBenchmark --warmup 1
                                                            --samples 2
    )
    set_tests_properties(benchmark::memory-alloc-gc-smoke PROPERTIES LABELS benchmark)
  endif()
endif()
