# ============================================================================
# LunaSVG — small standalone SVG rasterizer (MIT).
#
# Used by apps/chess to rasterize the embedded Cburnett piece SVGs (apps/chess/pieces/PieceSvg.h)
# into WebGPU textures at startup, so the demos ship no image asset files and work identically on
# every platform.
#
# Provides: lunasvg::lunasvg (static), include <lunasvg.h>
# ============================================================================

string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME lunasvg
  GITHUB_REPOSITORY sammycage/lunasvg
  GIT_TAG v3.5.0
  OPTIONS "LUNASVG_BUILD_EXAMPLES OFF" "USE_SYSTEM_PLUTOVG OFF" "BUILD_SHARED_LIBS OFF"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTALUNASVG "${AFTER} - ${BEFORE}")
message(STATUS "LunaSVG TIME: ${DELTALUNASVG}s")
