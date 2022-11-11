#ifndef RANDOM_H
#define RANDOM_H

#include <random>

struct Random {
  // inclusive
  static float Range(float start, float end){
    if(start == end)
      return start;
    std::random_device rd; // todo: random device is slow and the seed should be cached
    std::mt19937 mt(rd());
    std::uniform_real_distribution<float> dist(start, end);
    return dist(mt);
  }

    static int Range(int start, int end){
      if(start==end)
        return start;
      std::random_device rd; // todo: random device is slow and the seed should be cached
      std::mt19937 rng(rd());
      std::uniform_int_distribution<int> dist(start, end);
      return dist(rng);
    }
};

#endif
