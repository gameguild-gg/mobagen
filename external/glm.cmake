# GLM
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
  NAME GLM
  GITHUB_REPOSITORY g-truc/glm
  GIT_TAG 1.0.1
)
include_directories(${GLM_SOURCE_DIR})
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGLM "${AFTER} - ${BEFORE}")
message(STATUS "GLM TIME: ${DELTAGLM}s")
