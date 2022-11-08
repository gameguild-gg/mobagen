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

  inline constexpr double Fade(const double t) noexcept
  {
    return t * t * t * (t * (t * 6 - 15) + 10);
  }

  inline constexpr double Lerp(const double a, const double b, const double t) noexcept
  {
    return (a + (b - a) * t);
  }

  inline constexpr double Grad(const std::uint64_t hash, const double x, const double y, const double z) noexcept
  {
    const std::uint8_t h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : h == 12 || h == 14 ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
  }

 public:
  explicit Noise(int64_t seed, int64_t maxSamples = 1024, double minValue=-1, double maxValue=1);
  double noise(double x, double y=1, double z=1);
  double octave(int octaves, double persistence, double x, double y=0, double z=0);
};

#endif  // MOBAGEN_NOISE_H
