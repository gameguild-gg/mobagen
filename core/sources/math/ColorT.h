#pragma once
// Compatibility shim: ColorT/Color/Color32 were removed in W0-A.
// Several apps retain legacy #include "math/ColorT.h" with Color32::LerpColor,
// Color::DarkBlue, etc.  This file restores the legacy API backed by floats
// while remaining implicitly interoperable with glm::vec4.
#include <glm/glm.hpp>
#include <cstdint>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Color32 — float-channel RGBA with legacy static helpers.
// ---------------------------------------------------------------------------
struct Color32 {
  float r = 0.f, g = 0.f, b = 0.f, a = 1.f;

  constexpr Color32() = default;
  constexpr Color32(float r, float g, float b, float a = 1.f) : r(r), g(g), b(b), a(a) {}
  constexpr Color32(const glm::vec4& v) : r(v.x), g(v.y), b(v.z), a(v.w) {}  // NOLINT

  constexpr operator glm::vec4() const { return {r, g, b, a}; }
  Color32& operator=(const glm::vec4& v) {
    r = v.x;
    g = v.y;
    b = v.z;
    a = v.w;
    return *this;
  }

  // Legacy packed RGBA (R in byte-0, A in byte-3) — used for CPU pixel buffers.
  uint32_t GetPacked() const {
    auto c = [](float f) -> uint32_t { return f < 0.f ? 0u : (f > 1.f ? 255u : static_cast<uint32_t>(f * 255.f)); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
  }

  // Legacy linear interpolation between two colours.
  static Color32 LerpColor(const Color32& a, const Color32& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
  }

  Color32 Light() const { return {r + (1.f - r) * 0.5f, g + (1.f - g) * 0.5f, b + (1.f - b) * 0.5f, a}; }

  static Color32 RandomColor(uint8_t minVal, uint8_t maxVal) {
    auto rn = [minVal, maxVal]() -> float {
      int range = static_cast<int>(maxVal) - static_cast<int>(minVal) + 1;
      return (minVal + (std::rand() % range)) / 255.f;
    };
    return {rn(), rn(), rn(), 1.f};
  }
};

// ---------------------------------------------------------------------------
// Color — Color32 with named constants matching the old Color class API.
// Usage:  Color::DarkBlue,  Color::White, etc.
// ---------------------------------------------------------------------------
struct Color : Color32 {
  using Color32::Color32;

  static constexpr Color32 White = {1.00f, 1.00f, 1.00f};
  static constexpr Color32 Black = {0.00f, 0.00f, 0.00f};
  static constexpr Color32 Red = {1.00f, 0.00f, 0.00f};
  static constexpr Color32 Green = {0.00f, 0.50f, 0.00f};
  static constexpr Color32 Blue = {0.00f, 0.00f, 1.00f};
  static constexpr Color32 DarkBlue = {0.00f, 0.00f, 0.50f};
  static constexpr Color32 Yellow = {1.00f, 1.00f, 0.00f};
  static constexpr Color32 Brown = {0.65f, 0.16f, 0.16f};
  static constexpr Color32 SandyBrown = {0.96f, 0.64f, 0.38f};
  static constexpr Color32 Cyan = {0.00f, 1.00f, 1.00f};
  static constexpr Color32 Magenta = {1.00f, 0.00f, 1.00f};
  static constexpr Color32 Purple = {0.50f, 0.00f, 0.50f};
  static constexpr Color32 Gray = {0.50f, 0.50f, 0.50f};
  static constexpr Color32 Orange = {1.00f, 0.65f, 0.00f};
};

// Alias used by some legacy files that included ColorT.h directly.
using ColorT = Color32;
