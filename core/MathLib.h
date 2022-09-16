#ifndef MATH_H
#define MATH_H

#include <cmath>

namespace Mathf {
    // Normalizes any number to an arbitrary range
    // by assuming the range wraps around when going below min or above max
    static float normalize( const float value, const float start, const float end ) {
        const float width       = end - start   ;   //
        const float offsetValue = value - start ;   // value relative to 0

        return ( offsetValue - ( std::floor( offsetValue / width ) * width ) ) + start ;
        // + start to reset back to start of original range
    }
};


#endif
