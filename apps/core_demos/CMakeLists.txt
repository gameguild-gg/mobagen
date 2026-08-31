# Core examples/tests/benches.
#
# These targets intentionally live outside core/. They prove the core modules work, but they are not
# part of the library ABI or renderer dependency graph.

function(mobagen_core_example name)
  set(_sources ${ARGN})
  list(TRANSFORM _sources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/sources/")
  add_executable(${name} ${_sources})
  target_compile_features(${name} PRIVATE cxx_std_23)
endfunction()

function(mobagen_jobs_web_options name)
  if(EMSCRIPTEN)
    # Single-threaded wasm build: the Scheduler runs inline and app loops yield via
    # SDL_Delay/emscripten_sleep (asyncify). No PROXY_TO_PTHREAD.
    target_link_options(${name} PRIVATE -sEXIT_RUNTIME=1)
    set_target_properties(${name} PROPERTIES SUFFIX ".html")
  endif()
endfunction()

# ecs
foreach(t ecs_demo world_demo ecs_bench ecs_group_bench)
  mobagen_core_example(${t} ecs/${t}.cpp)
  target_link_libraries(${t} PRIVATE mobagen::core_ecs)
endforeach()

# input
mobagen_core_example(input_state_test input/input_state_test.cpp)
target_link_libraries(input_state_test PRIVATE mobagen::core_input)

# jobs
foreach(t jobs_demo jobs_bench jobs_c_demo)
  mobagen_core_example(${t} jobs/${t}.cpp)
  target_link_libraries(${t} PRIVATE mobagen::core_jobs)
  mobagen_jobs_web_options(${t})
endforeach()

# messaging / reactive / net / resource
mobagen_core_example(messaging_demo messaging/messaging_demo.cpp)
target_link_libraries(messaging_demo PRIVATE mobagen::core_messaging)

mobagen_core_example(reactive_demo reactive/reactive_demo.cpp)
target_link_libraries(reactive_demo PRIVATE mobagen::core_reactive)

mobagen_core_example(net_demo net/net_demo.cpp)
target_link_libraries(net_demo PRIVATE mobagen::core_net)

mobagen_core_example(resource_registry_test resource/resource_registry_test.cpp)
target_link_libraries(resource_registry_test PRIVATE mobagen::core_resource)

# scene
mobagen_core_example(scene_demo scene/scene_demo.cpp)
target_link_libraries(scene_demo PRIVATE mobagen::core_scene)

mobagen_core_example(scene_bench scene/scene_bench.cpp)
target_link_libraries(scene_bench PRIVATE mobagen::core_scene)
target_compile_definitions(scene_bench PRIVATE GLM_FORCE_INTRINSICS)
if(MSVC)
  target_compile_options(scene_bench PRIVATE /arch:AVX2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  target_compile_options(scene_bench PRIVATE -mavx2)
endif()

# render bridge / serialization
mobagen_core_example(render_bridge_demo render/render_bridge_demo.cpp)
target_link_libraries(render_bridge_demo PRIVATE mobagen::core_render)

mobagen_core_example(scene_serialize_test render/scene_serialize_test.cpp)
target_link_libraries(scene_serialize_test PRIVATE mobagen::core_render)

# composition examples glue several independent modules together.
mobagen_core_example(systems_demo composition/systems_demo.cpp)
target_link_libraries(systems_demo PRIVATE mobagen::core_ecs mobagen::core_jobs)
mobagen_jobs_web_options(systems_demo)

mobagen_core_example(engine_demo composition/engine_demo.cpp)
target_link_libraries(
  engine_demo PRIVATE mobagen::core_ecs mobagen::core_jobs mobagen::core_messaging
                      mobagen::core_reactive mobagen::core_scene
)
mobagen_jobs_web_options(engine_demo)

mobagen_core_example(dod_engine_slice composition/dod_engine_slice.cpp)
target_link_libraries(
  dod_engine_slice PRIVATE mobagen::core_ecs mobagen::core_input mobagen::core_render
                           mobagen::core_resource mobagen::core_scene
)

add_custom_target(
  mobagen_core_examples
  DEPENDS ecs_demo
          world_demo
          ecs_bench
          ecs_group_bench
          input_state_test
          jobs_demo
          jobs_bench
          jobs_c_demo
          messaging_demo
          reactive_demo
          net_demo
          resource_registry_test
          scene_demo
          scene_bench
          render_bridge_demo
          scene_serialize_test
          systems_demo
          engine_demo
          dod_engine_slice
)
