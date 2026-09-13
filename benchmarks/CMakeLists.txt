add_executable(MobagenFoundationBenchmark foundation_baseline.cpp)
target_compile_features(MobagenFoundationBenchmark PRIVATE cxx_std_20)
target_include_directories(
  MobagenFoundationBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
)
target_link_libraries(
  MobagenFoundationBenchmark
  PRIVATE mobagen::core_ecs mobagen::core_jobs mobagen::core_scene mobagen::core_render
)

if(ENABLE_TEST_COVERAGE)
  add_test(
    NAME benchmark::foundation-smoke
    COMMAND MobagenFoundationBenchmark --warmup 1 --samples 2
  )
  set_tests_properties(benchmark::foundation-smoke PROPERTIES LABELS benchmark)
endif()
