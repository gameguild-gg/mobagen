add_executable(MobagenFoundationBenchmark foundation_baseline.cpp)
target_compile_features(MobagenFoundationBenchmark PRIVATE cxx_std_20)
target_include_directories(
  MobagenFoundationBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
)
target_link_libraries(
  MobagenFoundationBenchmark PRIVATE mobagen::core_ecs mobagen::core_jobs mobagen::core_scene
                                     mobagen::core_render
)

add_executable(MobagenModuleDispatchBenchmark module_dispatch.cpp)
target_compile_features(MobagenModuleDispatchBenchmark PRIVATE cxx_std_20)
target_include_directories(
  MobagenModuleDispatchBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
)
target_link_libraries(MobagenModuleDispatchBenchmark PRIVATE mobagen::core_modules)

if(BUILD_TESTING)
  add_test(NAME benchmark::foundation-smoke COMMAND MobagenFoundationBenchmark --warmup 1 --samples
                                                    2
  )
  set_tests_properties(benchmark::foundation-smoke PROPERTIES LABELS benchmark)
  add_test(NAME benchmark::module-dispatch-smoke COMMAND MobagenModuleDispatchBenchmark --warmup 1
                                                         --samples 2
  )
  set_tests_properties(benchmark::module-dispatch-smoke PROPERTIES LABELS benchmark)
endif()
