#include "Noise.h"

Noise::Noise(int64_t seed, int64_t maxSamples, double minValue, double maxValue) {
  generator = std::mt19937_64(seed);
  distribution = std::uniform_real_distribution(minValue, maxValue);
  samples.reserve(maxSamples);
  for(auto i=0; i<maxSamples;i++)
    samples.push_back(distribution(generator));
}
double Noise::noise(double x, double y, double z) {
  // hasher
  // todo: improve spacing for each axis se each one do not follow the same behavior
  // todo: hashing is enough?
  auto hasher = std::hash<double>{};

  // clamped indexes
  uint64_t ix, iy, iz;
  ix = hasher(std::floor(x)) % samples.size();
  iy = hasher(std::floor(y)) % samples.size() ;
  iz = hasher(std::floor(z)) % samples.size();

  // clamped next indexes
  uint64_t nix, niy, niz;
  nix = (hasher(std::floor(x))+1) % samples.size();
  niy = (hasher(std::floor(y))+1) % samples.size() ;
  niz = (hasher(std::floor(z))+1) % samples.size();

  // what is after the . from the indexes
  double fx, fy, fz;
  fx = x - std::floor(x);
  fy = y - std::floor(y);
  fz = z - std::floor(z);

  // interpolated values
  double vx, vy, vz;
  vx = samples[ix]*(1-fx) + samples[nix]*(fx);
  vy = samples[iy]*(1-fy) + samples[niy]*(fy);
  vz = samples[iz]*(1-fz) + samples[niz]*(fz);

  // merge
  // todo: improve. it gonna give spherical behaviours.
  //  this will effectivelly clamp and reduce the amplitude
  return (vx + vy + vz) / 3;
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
