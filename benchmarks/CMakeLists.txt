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

if(NOT EMSCRIPTEN)
  add_library(MobagenAbiDispatchBenchmarkPlugin MODULE plugin_abi_dispatch_plugin.c)
  target_link_libraries(MobagenAbiDispatchBenchmarkPlugin PRIVATE mobagen::plugin_sdk)
  target_compile_definitions(MobagenAbiDispatchBenchmarkPlugin PRIVATE MOBAGEN_PLUGIN_BUILD)
  set_target_properties(MobagenAbiDispatchBenchmarkPlugin PROPERTIES PREFIX "")

  add_executable(MobagenPluginAbiDispatchBenchmark plugin_abi_dispatch.cpp)
  add_dependencies(MobagenPluginAbiDispatchBenchmark MobagenAbiDispatchBenchmarkPlugin)
  target_compile_features(MobagenPluginAbiDispatchBenchmark PRIVATE cxx_std_20)
  target_compile_definitions(
    MobagenPluginAbiDispatchBenchmark
    PRIVATE MOBAGEN_ABI_DISPATCH_PLUGIN_PATH="$<TARGET_FILE:MobagenAbiDispatchBenchmarkPlugin>"
  )
  target_include_directories(
    MobagenPluginAbiDispatchBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core/sources
  )
  target_link_libraries(MobagenPluginAbiDispatchBenchmark PRIVATE mobagen::core_plugins)
endif()

if(BUILD_TESTING)
  add_test(NAME benchmark::foundation-smoke COMMAND MobagenFoundationBenchmark --warmup 1 --samples
                                                    2
  )
  set_tests_properties(benchmark::foundation-smoke PROPERTIES LABELS benchmark)
  add_test(NAME benchmark::module-dispatch-smoke COMMAND MobagenModuleDispatchBenchmark --warmup 1
                                                         --samples 2
  )
  set_tests_properties(benchmark::module-dispatch-smoke PROPERTIES LABELS benchmark)
  if(TARGET MobagenPluginAbiDispatchBenchmark)
    add_test(NAME benchmark::plugin-abi-dispatch-smoke COMMAND MobagenPluginAbiDispatchBenchmark
                                                               --warmup 1 --samples 2
    )
    set_tests_properties(benchmark::plugin-abi-dispatch-smoke PROPERTIES LABELS benchmark)
  endif()
endif()
