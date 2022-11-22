# GLM
string(TIMESTAMP BEFORE "%s")
CPMAddPackage(
        NAME GLM
        GITHUB_REPOSITORY g-truc/glm
        GIT_TAG 0.9.9.8
)
include_directories(${GLM_SOURCE_DIR})
string(TIMESTAMP AFTER "%s")
math(EXPR DELTAGLM "${AFTER} - ${BEFORE}")
MESSAGE(STATUS "GLM TIME: ${DELTAGLM}s")