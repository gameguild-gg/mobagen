string(TIMESTAMP BEFORE "%s")
# Official source tarball pin (NOT the GitHub mirror: lua/lua's tags mark
# release commits but the tarball is the canonical distribution the
# sha256 below refers to). DOWNLOAD_ONLY: the guest builds the sources
# freestanding itself (modules/scripting_lua/CMakeLists.txt); upstream's
# own Makefile targets hosted builds we do not use.
CPMAddPackage(
  NAME lua
  URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
  URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
  DOWNLOAD_ONLY YES
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAlua "${AFTER} - ${BEFORE}")
message(STATUS "lua TIME: ${DELTAlua}s")
if(NOT EXISTS "${lua_SOURCE_DIR}/src/lua.h")
  message(FATAL_ERROR "lua ${lua_VERSION} did not provide src/lua.h")
endif()
