# curl
string(TIMESTAMP BEFORE "%s")

CPMAddPackage(
  GITHUB_REPOSITORY curl/curl
  # GIT_TAG curl-7_88_1
  GIT_TAG curl-7_80_0
  OPTIONS "CURL_USE_MBEDTLS ON"
          "CMAKE_USE_MBEDTLS ON"
          "MBEDTLS_INCLUDE_DIRS ${mbedtls_SOURCE_DIR}/include"
          "MBEDTLS_LIBRARY mbedtls"
          "MBEDX509_LIBRARY mbedcrypto"
          "MBEDCRYPTO_LIBRARY mbedx509"
          "BUILD_TESTING OFF"
          "CURL_CA_FALLBACK 1"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAcurl "${AFTER} - ${BEFORE}")
message(STATUS "curl TIME: ${DELTAcurl}s")
if(curl_ADDED)
  include_directories(${curl_SOURCE_DIR}/include)
else()
  message(FATAL_ERROR "curl not configured correctly")
endif()
