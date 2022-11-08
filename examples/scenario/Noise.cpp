#include "Noise.h"

Noise::Noise(int64_t seed, int64_t maxSamples, double minValue, double maxValue) {
  generator = std::mt19937_64(seed);
  distribution = std::uniform_real_distribution(minValue, maxValue);
  samples.reserve(maxSamples);
  for(auto i=0; i<maxSamples;i++)
    samples.push_back(distribution(generator));
}
// todo: this function is not production ready yet. Needs lots of improvements
double Noise::noise(double x, double y, double z) {
  // hasher
  // todo: improve spacing for each axis se each one do not follow the same behavior
  // todo: hashing is enough?
  auto hasher = std::hash<double>{};

  // clamped indexes
  uint64_t ix, iy, iz;
  ix = uint64_t(std::floor(x)) % samples.size();
  iy = uint64_t(std::floor(y)) % samples.size();
  iz = uint64_t(std::floor(z)) % samples.size();

  // what is after the . from the indexes
  double fx, fy, fz;
  fx = x - std::floor(x);
  fy = y - std::floor(y);
  fz = z - std::floor(z);

  // todo: gradient implementation goes here
  // todo: improve the dispersion and merging algorithm
  // lerp
  auto lx = samples[ix]*(1-fx) + samples[ix+1]*(fx);
  auto ly = samples[iy]*(1-fy) + samples[iy+1]*(fy);
  auto lz = samples[iz]*(1-fz) + samples[iz+1]*(fz);

  // todo: improve this merger function and dispersion
  return (lx+ly+lz)/3;
}
double Noise::octave(int octaves, double persistence, double x, double y, double z) {
  double result = 0;
  double amplitude = 1;

  for (std::int32_t i = 0; i < octaves; ++i)
  {
    result += (noise(x, y, z) * amplitude);
    x *= 2;
    y *= 2;
    z *= 2;
    amplitude *= persistence;
  }

  // todo: fix it wont obey the limits of max and min value of the distribution
  return result;
}
