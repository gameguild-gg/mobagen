# GLM
string(TIMESTAMP BEFORE "%s")
#todo: upgrade version to latest
CPMAddPackage(
        NAME GDCM
        GITHUB_REPOSITORY malaterre/GDCM
        GIT_TAG v2.8.9
        OPTIONS "GDCM_BUILD_SHARED_LIBS OFF" "GDCM_INSTALL_NO_DOCUMENTATION OFF" "GDCM_DOCUMENTATION OFF"
)
include_directories(
        ${CMAKE_BINARY_DIR}/_deps/gdcm-build/Source/Common
        ${GDCM_SOURCE_DIR}/Source/Common
        ${GDCM_SOURCE_DIR}/Source/DataStructureAndEncodingDefinition
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGDCM "${AFTER} - ${BEFORE}")
MESSAGE(STATUS "GDCM TIME: ${DELTAGDCM}s")