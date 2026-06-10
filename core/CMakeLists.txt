# Core is the shared engine library layer. All C++ sources and headers live
# under core/sources; demos and prototypes live under apps.

add_subdirectory(sources/camera)
add_subdirectory(sources/ecs)
add_subdirectory(sources/input)
add_subdirectory(sources/jobs)
add_subdirectory(sources/messaging)
add_subdirectory(sources/net)
add_subdirectory(sources/reactive)
add_subdirectory(sources/resource)
add_subdirectory(sources/scene)
add_subdirectory(sources/render)

# Aggregate target for Makefile/CI. Header-only modules have no object files, so
# the only concrete dependency today is the jobs static library.
add_custom_target(mobagen_core)
add_dependencies(mobagen_core mobagen_core_jobs)
