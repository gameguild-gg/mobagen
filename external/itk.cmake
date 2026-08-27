# ============================================================================
# ITK — Insight Toolkit (medical image processing).
# ============================================================================
# NOT used by the renderer yet. Brought over from master:editor/itk.cmake for completeness. ITK
# bundles its own copy of GDCM, so enabling USE_ITK is a (much heavier) alternative to USE_GDCM if
# we ever need ITK's resampling / registration. Included only when USE_ITK is ON.

string(TIMESTAMP _itk_before "%s")

CPMAddPackage(
  NAME ITK
  GITHUB_REPOSITORY InsightSoftwareConsortium/ITK
  GIT_TAG v5.3rc04
  OPTIONS "ITK_USE_CLANG_FORMAT OFF" "BUILD_DOC OFF"
)

string(TIMESTAMP _itk_after "%s")
math(EXPR _itk_delta "${_itk_after} - ${_itk_before}")
message(STATUS "ITK configured in ${_itk_delta}s")

# ITK ships GDCM under ThirdParty/GDCM — expose those headers so code that uses ITK's GDCM resolves
# them. (See master:editor/itk.cmake for the original paths.)
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
  ${ITKGDCM_BINARY_DIR}
)
