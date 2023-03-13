# cpr
string(TIMESTAMP BEFORE "%s")

CPMAddPackage(
  GITHUB_REPOSITORY libcpr/cpr
  GIT_TAG 1.10.1
  OPTIONS "CPR_ENABLE_SSL ON" "CPR_FORCE_MBEDTLS_BACKEND ON" "CURL_USE_MBEDTLS ON"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAcpr "${AFTER} - ${BEFORE}")
message(STATUS "cpr TIME: ${DELTAcpr}s")
if(cpr_ADDED)
  include_directories(${cpr_SOURCE_DIR}/include)
  include_directories(${cpr_BUILD_DIR}/cpr_generated_includes)
else()
  message(FATAL_ERROR "cpr not configured correctly")
endif()
