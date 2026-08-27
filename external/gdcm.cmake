# ============================================================================
# GDCM — Grassroots DICOM library, built from source via CPM.
# ============================================================================
# Parses DICOM files/series. Consumed by the volume_io module (Tier 3-A); the renderer never sees
# GDCM, only volume_io's flat C struct.
#
# Brought over from master:editor/gdcm.cmake. Static build, no docs/tests/apps/ language wrappings
# (we only need the C++ reader). Included only when USE_GDCM is ON so the heavy build never slows a
# normal renderer iteration.
#
# Version note (Windows vs Linux): the researcher used v2.8.9 on Linux/gcc, but that 2018 tag does
# NOT build on a current MSVC STL — its IPPSorter comparator (gdcm::dircos_comp::operator()) is
# non-const, which modern std::map/set reject (error C2662). v3.0.x fixes this; the Source/ layout
# and target names are unchanged, so the include dirs below and volume_io.cpp still apply.

string(TIMESTAMP _gdcm_before "%s")

CPMAddPackage(
  NAME GDCM
  GITHUB_REPOSITORY malaterre/GDCM
  GIT_TAG v3.0.24
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

# GDCM splits its public headers across several source trees; expose the ones a reader needs. The
# build tree (_deps/gdcm-build/Source/Common) holds generated headers (gdcmConfigure.h etc.).
add_library(gdcm_headers INTERFACE)
target_include_directories(
  gdcm_headers
  INTERFACE ${CMAKE_BINARY_DIR}/_deps/gdcm-build/Source/Common
            ${GDCM_SOURCE_DIR}/Source/Common
            ${GDCM_SOURCE_DIR}/Source/DataStructureAndEncodingDefinition
            ${GDCM_SOURCE_DIR}/Source/MediaStorageAndFileFormat
            ${GDCM_SOURCE_DIR}/Source/DataDictionary
            ${GDCM_SOURCE_DIR}/Source/InformationObjectDefinition
)

string(TIMESTAMP _gdcm_after "%s")
math(EXPR _gdcm_delta "${_gdcm_after} - ${_gdcm_before}")
message(
  STATUS "GDCM configured in ${_gdcm_delta}s (link: gdcmMSFF gdcmDSED gdcmDICT gdcmIOD gdcmCommon)"
)
