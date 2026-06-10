string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME ITX
  GITHUB_REPOSITORY InsightSoftwareConsortium/ITK
  GIT_TAG v5.3rc04
  OPTIONS "ITK_USE_CLANG_FORMAT OFF" "BUILD_DOC OFF"
)
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAITK "${AFTER} - ${BEFORE}")
message(STATUS "ITK TIME: ${DELTAITK}s")

# todo: add gdcm source dir to the include path
message(STATUS "ITK_SOURCE_DIR: ${ITK_SOURCE_DIR}")
include_directories(
  ${ITK_SOURCE_DIR}
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/MediaStorageAndFileFormat
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/DataDictionary
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/Common
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/InformationObjectDefinition
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/MessageExchangeDefinition
  ${ITKGDCM_SOURCE_DIR}/src/gdcm/Source/DataStructureAndEncodingDefinition
  ${ITKGDCM_BINARY_DIR}/src/gdcm/Source/Common
)
# C:\projects\mobagen\cmake-debug\_deps\itx-build\Modules\ThirdParty\GDCM\src\gdcm\Source\Common
message(STATUS "ITKGDCM_SOURCE_DIR: ${ITKGDCM_SOURCE_DIR}")
message(STATUS "ITKGDCM_BINARY_DIR: ${ITKGDCM_BINARY_DIR}")
include_directories(${ITKGDCM_BINARY_DIR})
