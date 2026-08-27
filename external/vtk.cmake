# ============================================================================
# VTK — Visualization Toolkit.
# ============================================================================
# NOT used in the build. VTK ships a full GPU volume renderer, which would directly compete with the
# renderer this project exists to write — so it is kept only as a reference / benchmark dependency,
# never linked by default. Brought over from master:editor/vtk.cmake. Included only when USE_VTK is
# ON.

string(TIMESTAMP _vtk_before "%s")

CPMAddPackage(
  NAME VTK
  GITHUB_REPOSITORY Kitware/VTK
  GIT_TAG v9.2.2
  OPTIONS "BUILD_SHARED_LIBS OFF"
)

string(TIMESTAMP _vtk_after "%s")
math(EXPR _vtk_delta "${_vtk_after} - ${_vtk_before}")
message(STATUS "VTK configured in ${_vtk_delta}s")
