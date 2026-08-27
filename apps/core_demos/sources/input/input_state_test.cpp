// Round-trip/edge test for input::InputState.
// Native toolchain is currently unavailable, so build with em++ + run under node:
//   em++ -std=c++20 apps/core_demos/sources/input/input_state_test.cpp -I core/sources/input -o build/input_test.js
//   node build/input_test.js
#include "input_state.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  input::InputState in;
  constexpr std::uint32_t KEY_A = 97;  // 'a' (SDLK_a-like; core doesn't care)

  // Frame 1: press A -> edge "pressed" + "held", not "released".
  in.begin_frame();
  in.on_key(KEY_A, true);
  assert(in.pressed(KEY_A));
  assert(in.held(KEY_A));
  assert(!in.released(KEY_A));

  // Frame 2: no events -> still held, but no longer a fresh "pressed" edge.
  in.begin_frame();
  assert(in.held(KEY_A));
  assert(!in.pressed(KEY_A));

  // Frame 3: release A -> "released" edge, no longer held.
  in.begin_frame();
  in.on_key(KEY_A, false);
  assert(in.released(KEY_A));
  assert(!in.held(KEY_A));

  // Mouse: button edges, accumulated motion delta, accumulated wheel.
  in.begin_frame();
  in.on_mouse_button(0, true);
  in.on_mouse_move(100.0f, 50.0f, 4.0f, -2.0f);
  in.on_mouse_move(103.0f, 49.0f, 3.0f, -1.0f);
  in.on_wheel(1.0f);
  in.on_wheel(0.5f);
  assert(in.mouse_pressed(0));
  assert(in.mouse_held(0));
  assert(std::fabs(in.mouse_dx() - 7.0f) < 1e-6f);  // 4 + 3
  assert(std::fabs(in.mouse_dy() + 3.0f) < 1e-6f);  // -2 + -1
  assert(std::fabs(in.mouse_x() - 103.0f) < 1e-6f);
  assert(std::fabs(in.wheel() - 1.5f) < 1e-6f);

  // Next frame clears edges + deltas; held button persists.
  in.begin_frame();
  assert(!in.mouse_pressed(0));
  assert(in.mouse_held(0));
  assert(std::fabs(in.mouse_dx()) < 1e-6f);
  assert(std::fabs(in.wheel()) < 1e-6f);

  in.on_mouse_button(0, false);
  assert(in.mouse_released(0));
  assert(!in.mouse_held(0));

  std::printf(
      "input_state OK: key + mouse edges, held persistence, "
      "delta/wheel accumulation + per-frame reset verified\n");
  return 0;
}
