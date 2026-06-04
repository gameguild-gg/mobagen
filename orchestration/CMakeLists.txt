# ============================================================================
# Orchestration core — data-oriented engine modules (jobs / ecs / reactive /
# messaging / scene / net) + cross-module demos.
#
# Brought from branch feat/wasm-dicom-raytracing-renderer to AUGMENT master's
# OOP core/ (GameObject/Component). It lives entirely under orchestration/ and
# touches nothing in core/, editor/, modules/, test/, examples/ — so master's
# normal build + CI are unaffected. Enabled via the top-level BUILD_ORCHESTRATION
# option (OFF by default).
#
# These modules are pure C++20 (+ glm for scene); no SDL3/Dawn/ImGui.
# ============================================================================

if(NOT TARGET glm)
    include(${CMAKE_SOURCE_DIR}/external/glm.cmake)
endif()

# jobs builds for native + web (pthreads). The rest of the demos are native-only.
if(NOT EMSCRIPTEN)
    add_subdirectory(ecs)
    add_subdirectory(reactive)
    add_subdirectory(messaging)
    add_subdirectory(scene)
    add_subdirectory(net)
    add_subdirectory(examples)   # systems-as-jobs + engine tick (composes all)
endif()
add_subdirectory(jobs)
