string(TIMESTAMP BEFORE "%s")
# NOTE: V8 version 8.9.255.20 is outdated (5+ years old) Latest is v13.0+, but represents major API
# changes Consider evaluating if V8 is still needed or should be replaced
CPMAddPackage("gh:bnoordhuis/v8-cmake#8.9.255.20")
include_directories(${v8-cmake_SOURCE_DIR})
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAv8-cmake "${AFTER} - ${BEFORE}")
message(STATUS "v8-cmake TIME: ${DELTAv8-cmake}s")
