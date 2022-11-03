#ifndef MOBAGEN_NOISE_H
#define MOBAGEN_NOISE_H

# include <cstdint>
# include <algorithm>
# include <array>
# include <iterator>
# include <numeric>
# include <random>
# include <type_traits>

class Noise {
 private:
  std::mt19937_64 generator;
  std::uniform_real_distribution<double> distribution;
  std::vector<double> samples;
 public:
  explicit Noise(int64_t seed, int64_t maxSamples = 1024, double minValue=-1, double maxValue=1);
  double noise(double x, double y=1, double z=1);
  double octave(int octaves, double persistence, double x, double y=0, double z=0);
};

#endif  // MOBAGEN_NOISE_H
