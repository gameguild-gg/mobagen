# GLM: Header-only math library for graphics (vectors, matrices, quaternions)
# Used by abstraction layer for uniform setters (glm::vec3, glm::mat4, etc.)

CPMAddPackage(
    NAME glm
    GITHUB_REPOSITORY g-truc/glm
    GIT_TAG 1.0.1
)

# GLM is header-only, so we just expose the include directory
# Create target even if package was already fetched
if(NOT TARGET glm)
    add_library(glm INTERFACE IMPORTED)
    target_include_directories(glm INTERFACE ${glm_SOURCE_DIR})
endif()
