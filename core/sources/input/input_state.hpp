#pragma once
// ============================================================================
// InputState — SDL-agnostic input for DOD systems.
// ============================================================================
// A thin platform adapter (e.g. the SDL3 event loop in the renderer) feeds raw
// events in; systems query edges (pressed/released THIS frame), held state, and
// mouse deltas. The core never interprets keycodes — they are whatever the
// platform sends (SDLK_* in our case), so this stays engine-agnostic.
//
// This is CONTROL-PLANE state (a handful of keys/buttons per frame), not a hot
// per-element loop, so the small hashed sets are fine — locality discipline is
// spent where it matters (ECS/jobs/scene), not here.

#include <cstdint>
#include <unordered_set>

namespace input {

  class InputState {
  public:
    // Call once at the top of the frame, BEFORE feeding this frame's events:
    // clears per-frame edges + accumulated deltas; held state persists.
    void begin_frame() {
      pressed_.clear();
      released_.clear();
      mouse_pressed_ = 0;
      mouse_released_ = 0;
      mouse_dx_ = mouse_dy_ = 0.0f;
      wheel_ = 0.0f;
    }

    void on_key(std::uint32_t key, bool down) {
      const bool was = held_.count(key) != 0;
      if (down) {
        if (!was) pressed_.insert(key);
        held_.insert(key);
      } else {
        if (was) released_.insert(key);
        held_.erase(key);
      }
    }

    void on_mouse_move(float x, float y, float dx, float dy) {
      mouse_x_ = x;
      mouse_y_ = y;
      mouse_dx_ += dx;
      mouse_dy_ += dy;
    }

    void on_mouse_button(std::uint8_t button, bool down) {
      const std::uint32_t bit = 1u << button;
      if (down) {
        if (!(buttons_ & bit)) mouse_pressed_ |= bit;
        buttons_ |= bit;
      } else {
        if (buttons_ & bit) mouse_released_ |= bit;
        buttons_ &= ~bit;
      }
    }

    void on_wheel(float dy) { wheel_ += dy; }

    // --- queries ---------------------------------------------------------------
    bool held(std::uint32_t key) const { return held_.count(key) != 0; }
    bool pressed(std::uint32_t key) const { return pressed_.count(key) != 0; }
    bool released(std::uint32_t key) const { return released_.count(key) != 0; }

    bool mouse_held(std::uint8_t b) const { return (buttons_ & (1u << b)) != 0; }
    bool mouse_pressed(std::uint8_t b) const { return (mouse_pressed_ & (1u << b)) != 0; }
    bool mouse_released(std::uint8_t b) const { return (mouse_released_ & (1u << b)) != 0; }

    float mouse_x() const { return mouse_x_; }
    float mouse_y() const { return mouse_y_; }
    float mouse_dx() const { return mouse_dx_; }
    float mouse_dy() const { return mouse_dy_; }
    float wheel() const { return wheel_; }

  private:
    std::unordered_set<std::uint32_t> held_, pressed_, released_;
    std::uint32_t buttons_ = 0, mouse_pressed_ = 0, mouse_released_ = 0;
    float mouse_x_ = 0.0f, mouse_y_ = 0.0f;
    float mouse_dx_ = 0.0f, mouse_dy_ = 0.0f;
    float wheel_ = 0.0f;
  };

}  // namespace input
