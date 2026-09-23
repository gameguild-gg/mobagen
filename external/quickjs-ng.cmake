# quickjs-ng v0.10.0 (MIT) for the QuickJS wasm guest (todo 16 CPM rework).
# DOWNLOAD_ONLY: the guest builds the freestanding sources itself
# (-nostdlib + shadow headers); upstream build files are never used.
CPMAddPackage(
  NAME quickjs
  GITHUB_REPOSITORY quickjs-ng/quickjs
  GIT_TAG v0.10.0
  GIT_SHALLOW TRUE
  DOWNLOAD_ONLY YES
)
message(STATUS "quickjs-ng ${quickjs_VERSION} fetched to ${quickjs_SOURCE_DIR}")
if(NOT EXISTS "${quickjs_SOURCE_DIR}/quickjs.c")
  message(FATAL_ERROR "quickjs-ng ${quickjs_VERSION} did not provide quickjs.c")
endif()
