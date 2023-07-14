string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
        NAME quickjspp
        URL "https://github.com/ftk/quickjspp/archive/refs/heads/master.zip"
        OPTIONS "QUICKJSPP_BUILD_TESTS OFF" "BUILD_TESTING OFF"
)
if(quickjspp_ADDED)
#  add_library(quickjspp STATIC)
#  target_compile_definitions(
#          quickjs PRIVATE CONFIG_VERSION="v1.0.0" # hack to make it compile. it doesnt accept date as
#          # version number
#  )
#  target_sources(
#    quickjs
#    PRIVATE ${quickjs_SOURCE_DIR}/quickjs.h
#            ${quickjs_SOURCE_DIR}/quickjs-libc.h
#            ${quickjs_SOURCE_DIR}/quickjs.c
#            ${quickjs_SOURCE_DIR}/libregexp.c
#            ${quickjs_SOURCE_DIR}/libunicode.c
#            ${quickjs_SOURCE_DIR}/libbf.c
#            ${quickjs_SOURCE_DIR}/cutils.c
#            ${quickjs_SOURCE_DIR}/quickjs-libc.c
#  )
  include_directories(${quickjspp_SOURCE_DIR})
endif()
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAquickjspp "${AFTER} - ${BEFORE}")
message(STATUS "quickjspp TIME: ${DELTAquickjspp}s")
