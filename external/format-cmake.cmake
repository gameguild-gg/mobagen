# Format.cmake - adds the format-cmake / check-format-cmake targets (clang-format
# for C++ sources, cmake-format for CMake files). Used by the style CI. Added once
# here; consumers get the targets from the root configure.
# Note: named format-cmake.cmake because CPM's source cache already owns the
# external/format.cmake/ directory name.
CPMAddPackage("gh:TheLartians/Format.cmake@1.7.3")
