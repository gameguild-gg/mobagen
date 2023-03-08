# https://github.com/google/filament.git SET(ENABLE_PCH OFF CACHE BOOL "ENABLE_PCH")
set_property(GLOBAL PROPERTY SKIP_PRECOMPILE_HEADERS ON)
set_property(GLOBAL PROPERTY DISABLE_PRECOMPILE_HEADERS ON)
CPMAddPackage(
  GITHUB_REPOSITORY google/filament
  GIT_TAG v1.29.0
  OPTIONS "ENABLE_PCH OFF" "ENABLE_OPT OFF"
)
find_package(filament REQUIRED)
if(filament_ADDED)
  message(STATUS filament_ADDED)
endif()
