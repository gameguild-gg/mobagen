#pragma once
// Compatibility shim: Point2D was removed in W0-A.
// Grid2D.h retains a legacy #include "../math/Point2D.h"; this file resolves
// that dependency by aliasing Point2D to glm::ivec2 so Grid2D compiles
// without modification. New code should use glm::ivec2 directly.
#include <glm/glm.hpp>
using Point2D = glm::ivec2;
