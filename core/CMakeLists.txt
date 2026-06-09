# Core is the engine library layer.
#
# Rule for this directory:
#   - headers / library sources live under core/
#   - demos, tests, and benches live under examples/core/
#
# That keeps the core usable as a dependency of the renderer, editor, wasm
# module, or another language binding without accidentally compiling study apps.

add_subdirectory(ecs)
add_subdirectory(input)
add_subdirectory(jobs)
add_subdirectory(messaging)
add_subdirectory(net)
add_subdirectory(reactive)
add_subdirectory(resource)
add_subdirectory(scene)
add_subdirectory(render)

# Aggregate target for Makefile/CI. Header-only modules have no object files, so
# the only concrete dependency today is the jobs static library.
add_custom_target(mobagen_core)
add_dependencies(mobagen_core mobagen_core_jobs)
