# GLM
string(TIMESTAMP BEFORE "%s")
# todo: upgrade version to latest
CPMAddPackage(
  NAME GDCM
  GITHUB_REPOSITORY malaterre/GDCM
  GIT_TAG v2.8.9
  OPTIONS "GDCM_BUILD_SHARED_LIBS OFF"
          "GDCM_DOCUMENTATION OFF"
          "GDCM_BUILD_DOCBOOK_MANPAGES OFF"
          "GDCM_BUILD_TESTING OFF"
          "GDCM_BUILD_APPLICATIONS OFF"
          "GDCM_BUILD_EXAMPLES OFF"
          "GDCM_WRAP_CSHARP OFF"
          "GDCM_WRAP_JAVA OFF"
          "GDCM_WRAP_PYTHON OFF"
          "GDCM_USE_PVRG OFF"
)
include_directories(
  ${CMAKE_BINARY_DIR}/_deps/gdcm-build/Source/Common ${GDCM_SOURCE_DIR}/Source/Common
  ${GDCM_SOURCE_DIR}/Source/DataStructureAndEncodingDefinition
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGDCM "${AFTER} - ${BEFORE}")
message(STATUS "GDCM TIME: ${DELTAGDCM}s")
