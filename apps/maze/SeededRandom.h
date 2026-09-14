#ifndef MOBAGEN_SEEDEDRANDOM_H
#define MOBAGEN_SEEDEDRANDOM_H
#include <stdexcept>
#include <cstdint>

// Deterministic randomness for the formal maze assignment: the ONLY randomness
// source allowed. Seeded by an index into a fixed array and consumed strictly
// in order, so a given seed always produces the exact same maze.
struct SeededRandom {
private:
  static const inline uint8_t randomNumbers[100]
      = {72, 99, 56, 34, 43, 62, 31, 4,  70, 22, 06, 65, 96, 71, 29, 9,  98, 41, 90, 7,  30, 3,  97, 49, 63, 88, 47, 82, 91, 54, 74, 2,  86, 14,
         58, 35, 89, 11, 10, 60, 28, 21, 52, 50, 55, 69, 76, 94, 23, 66, 15, 57, 44, 18, 67, 5,  24, 33, 77, 53, 51, 59, 20, 42, 80, 61, 1,  0,
         38, 64, 45, 92, 46, 79, 93, 95, 37, 40, 83, 13, 12, 78, 75, 73, 84, 81, 8,  32, 27, 19, 87, 85, 16, 25, 17, 68, 26, 39, 48, 36};
  static inline uint8_t index = 0;

public:
  static uint8_t next() {
    // every time this is called, it should use the current index for the return then increment the index by 1 and wrap around to 0 if reaches the end of the array.
    // This a simple random number generator, we will use more robust random number generation later.
    throw new std::runtime_error("SeededRandom::next() is not implemented yet.");
    return 0;
  }

  static void setIndex(uint8_t i) { index = i; }
};

#endif  // MOBAGEN_SEEDEDRANDOM_H
