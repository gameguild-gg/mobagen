string(TIMESTAMP BEFORE "%s")
CPMAddPackage(NAME quickjs URL "https://github.com/bellard/quickjs/archive/refs/heads/master.zip")
if(quickjs_ADDED)
  add_library(quickjs STATIC)
  target_sources(
    quickjs
    PRIVATE ${quickjs_SOURCE_DIR}/quickjs.h
            ${quickjs_SOURCE_DIR}/quickjs-libc.h
            ${quickjs_SOURCE_DIR}/quickjs.c
            ${quickjs_SOURCE_DIR}/libregexp.c
            ${quickjs_SOURCE_DIR}/libunicode.c
            ${quickjs_SOURCE_DIR}/libbf.c
            ${quickjs_SOURCE_DIR}/cutils.c
            ${quickjs_SOURCE_DIR}/quickjs-libc.c
  )
  target_compile_definitions(
    quickjs PRIVATE CONFIG_VERSION="v1.0.0" # hack to make it compile. it doesnt accept date as
                                            # version number
  )
  include_directories(quickjs PUBLIC ${quickjs_SOURCE_DIR})
endif()
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAquickjs "${AFTER} - ${BEFORE}")
message(STATUS "quickjs TIME: ${DELTAquickjs}s")