#include "ShadowCastGridRecursive.h"

// reference: https://www.albertford.com/shadowcasting/
void ShadowCastGridRecursive(Grid2D<Square>& grid, Point2D& origin, int deepness, int maxDeepness, Octant octant, Slope slopeRange) {
  // return if max depth is reached
  if (deepness > maxDeepness) return;

  // return if the slope range is invalid
  if (slopeRange.min > slopeRange.max || slopeRange.min < 0 || slopeRange.max > 1) return;
  // for every octant, behave differently.

  // start the lower slope with the slopeRange.min
  // list all elements for the current level of deepness inside the slope range. to calculate the range, you will use origin, depth and octant
  // iterate over the list of elements looking for blocks:
  //   If a block is found, then call visibility again with the new slope range from the current lower and the current block.
  //   Search for the new lower slope, if the current block is not a wall, then the new lower slope is the current block's slope.
  //   If the current block has slope of 1 call visibility again with the new slope range from the current lower and the current block.
}