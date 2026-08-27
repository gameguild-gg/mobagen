#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace engine {

  // ============================================================================
  // CAMERA MODES
  // ============================================================================
  enum class CameraMode {
    ORBIT,  // Orbit around a focal point (for DICOM viewer)
    WASD    // Free-look FPS style (for game engine)
  };

  // Projection: perspective (natural 3D, with FOV) vs orthographic (parallel rays,
  // true-to-scale — radiologists often prefer this for measurement).
  enum class Projection { Perspective, Orthographic };

  // ============================================================================
  // CAMERA: Unified interface supporting Orbit + WASD modes
  // ============================================================================
  class Camera {
  public:
    Camera(CameraMode mode = CameraMode::ORBIT)
        : mode_(mode),
          position_(0.0f, 0.0f, 2.0f),
          focal_point_(0.0f, 0.0f, 0.0f),
          up_(0.0f, 1.0f, 0.0f),
          fov_(45.0f),
          aspect_(16.0f / 9.0f),
          near_(0.1f),
          far_(1000.0f),
          yaw_(0.0f),
          pitch_(0.0f),
          orbit_radius_(2.0f),
          move_speed_(5.0f),
          mouse_sensitivity_(0.005f) {
      update_view_matrix();
      update_projection_matrix();
    }

    // ========================================================================
    // GETTERS
    // ========================================================================

    glm::mat4 get_view_matrix() const { return view_matrix_; }
    glm::mat4 get_projection_matrix() const { return projection_matrix_; }
    glm::mat4 get_view_projection() const { return projection_matrix_ * view_matrix_; }

    glm::vec3 get_position() const { return position_; }
    glm::vec3 get_focal_point() const { return focal_point_; }
    glm::vec3 get_forward() const { return glm::normalize(focal_point_ - position_); }
    glm::vec3 get_right() const { return glm::normalize(glm::cross(get_forward(), up_)); }
    glm::vec3 get_up() const { return up_; }

    float get_fov() const { return fov_; }
    float get_aspect() const { return aspect_; }
    float get_orbit_radius() const { return orbit_radius_; }
    float get_move_speed() const { return move_speed_; }
    float get_yaw_degrees() const { return glm::degrees(yaw_); }
    float get_pitch_degrees() const { return glm::degrees(pitch_); }

    CameraMode get_mode() const { return mode_; }

    // ========================================================================
    // SETTERS
    // ========================================================================

    void set_mode(CameraMode mode) { mode_ = mode; }

    void set_fov(float fov) {
      fov_ = glm::clamp(fov, 10.0f, 120.0f);
      update_projection_matrix();
    }

    Projection get_projection() const { return projection_; }
    void set_projection(Projection p) {
      projection_ = p;
      update_projection_matrix();
    }

    void set_aspect(float aspect) {
      aspect_ = aspect;
      update_projection_matrix();
    }

    void set_viewport(int width, int height) {
      if (height > 0) {
        aspect_ = static_cast<float>(width) / static_cast<float>(height);
        update_projection_matrix();
      }
    }

    // ========================================================================
    // INPUT HANDLERS (SDL2 events)
    // ========================================================================

    // Keyboard input. keys_pressed_ has 256 slots, but SDL keycodes for special
    // keys (arrows, shift, ...) are ~1e9, so we MUST bounds-check or we write
    // gigabytes out of bounds (a WASM "memory access out of bounds" trap). We
    // only track the ASCII keys WASD movement uses (w/a/s/d/space), all < 256.
    void on_key_pressed(int key_code) {
      key_code = normalize_key(key_code);
      if (mode_ == CameraMode::WASD && key_code >= 0 && key_code < 256) {
        keys_pressed_[key_code] = true;
      }
    }

    void on_key_released(int key_code) {
      key_code = normalize_key(key_code);
      if (key_code >= 0 && key_code < 256) {
        keys_pressed_[key_code] = false;
      }
    }

    // Mouse motion (for orbit or freelook yaw/pitch)
    void on_mouse_motion(int dx, int dy) {
      if (mode_ == CameraMode::ORBIT) {
        orbit_on_mouse_motion(dx, dy);
      } else if (mode_ == CameraMode::WASD) {
        wasd_on_mouse_motion(dx, dy);
      }
    }

    void on_mouse_pan(int dx, int dy) {
      const float scale = orbit_radius_ * 0.0015f;
      const glm::vec3 delta = (-get_right() * static_cast<float>(dx) + up_ * static_cast<float>(dy)) * scale;
      position_ += delta;
      focal_point_ += delta;
      update_view_matrix();
    }

    // Mouse wheel (zoom in/out)
    void on_mouse_wheel(int wheel_y) {
      if (mode_ == CameraMode::ORBIT) {
        orbit_radius_ -= wheel_y * 0.1f;
        orbit_radius_ = glm::clamp(orbit_radius_, 0.1f, 100.0f);
        update_view_matrix();
        update_projection_matrix();  // ortho extent tracks orbit_radius
      } else if (mode_ == CameraMode::WASD) {
        move_speed_ += wheel_y * 0.5f;
        move_speed_ = glm::clamp(move_speed_, 1.0f, 50.0f);
      }
    }

    // Update (call once per frame for WASD movement)
    void update(float delta_time) {
      if (mode_ == CameraMode::WASD) {
        wasd_update(delta_time);
      }
    }

    void set_descend_active(bool active) { descend_active_ = active; }

    // ========================================================================
    // RESET
    // ========================================================================

    void reset() {
      position_ = glm::vec3(0.0f, 0.0f, 2.0f);
      focal_point_ = glm::vec3(0.0f, 0.0f, 0.0f);
      yaw_ = 0.0f;
      pitch_ = 0.0f;
      orbit_radius_ = 2.0f;
      update_view_matrix();
    }

  private:
    static int normalize_key(int key_code) {
      if (key_code >= 'A' && key_code <= 'Z') return key_code + ('a' - 'A');
      return key_code;
    }

    // ========================================================================
    // ORBIT MODE IMPLEMENTATION
    // ========================================================================

    void orbit_on_mouse_motion(int dx, int dy) {
      yaw_ += dx * mouse_sensitivity_;
      pitch_ -= dy * mouse_sensitivity_;
      pitch_ = glm::clamp(pitch_, -glm::pi<float>() / 2.0f + 0.1f, glm::pi<float>() / 2.0f - 0.1f);
      update_view_matrix();
    }

    void update_orbit_camera() {
      float x = orbit_radius_ * std::sin(yaw_) * std::cos(pitch_);
      float y = orbit_radius_ * std::sin(pitch_);
      float z = orbit_radius_ * std::cos(yaw_) * std::cos(pitch_);
      position_ = focal_point_ + glm::vec3(x, y, z);
    }

    // ========================================================================
    // WASD MODE IMPLEMENTATION
    // ========================================================================

    void wasd_on_mouse_motion(int dx, int dy) {
      yaw_ += dx * mouse_sensitivity_;
      pitch_ -= dy * mouse_sensitivity_;
      pitch_ = glm::clamp(pitch_, -glm::pi<float>() / 2.0f + 0.1f, glm::pi<float>() / 2.0f - 0.1f);
      update_view_matrix();
    }

    void wasd_update(float delta_time) {
      glm::vec3 forward = get_forward();
      glm::vec3 right = get_right();
      glm::vec3 movement(0.0f);

      // W = 119, A = 97, S = 115, D = 100
      if (keys_pressed_[119]) movement += forward;  // W
      if (keys_pressed_[115]) movement -= forward;  // S
      if (keys_pressed_[100]) movement += right;    // D
      if (keys_pressed_[97]) movement -= right;     // A
      if (keys_pressed_[32]) movement += up_;       // Space
      if (descend_active_) movement -= up_;         // Shift/Ctrl

      // Normalize to prevent faster diagonal movement
      if (glm::length(movement) > 0.01f) {
        movement = glm::normalize(movement) * move_speed_ * delta_time;
        position_ += movement;
        focal_point_ += movement;  // Keep focal point ahead
      }

      update_view_matrix();
    }

    // ========================================================================
    // VIEW & PROJECTION MATRICES
    // ========================================================================

    void update_view_matrix() {
      if (mode_ == CameraMode::ORBIT) {
        update_orbit_camera();
      }
      // For WASD, position/focal_point are updated directly in wasd_update()

      view_matrix_ = glm::lookAt(position_, focal_point_, up_);
    }

    void update_projection_matrix() {
      if (projection_ == Projection::Orthographic) {
        // Match the perspective framing at the focal distance so toggling keeps
        // the volume the same on-screen size; wheel-zoom (orbit_radius) then
        // scales the ortho extent.
        const float dist = (mode_ == CameraMode::ORBIT) ? orbit_radius_ : glm::length(focal_point_ - position_);
        const float halfH = std::max(dist, 0.01f) * std::tan(glm::radians(fov_) * 0.5f);
        const float halfW = halfH * aspect_;
        projection_matrix_ = glm::ortho(-halfW, halfW, -halfH, halfH, near_, far_);
      } else {
        projection_matrix_ = glm::perspective(glm::radians(fov_), aspect_, near_, far_);
      }
    }

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    CameraMode mode_;

    // Position & orientation
    glm::vec3 position_;
    glm::vec3 focal_point_;
    glm::vec3 up_;

    // Euler angles
    float yaw_;
    float pitch_;

    // Orbit mode
    float orbit_radius_;

    // WASD mode
    float move_speed_;
    bool keys_pressed_[256] = {};  // Keyboard state
    bool descend_active_ = false;

    // Projection
    Projection projection_ = Projection::Perspective;
    float fov_;
    float aspect_;
    float near_;
    float far_;

    // Interaction
    float mouse_sensitivity_;

    // Matrices
    glm::mat4 view_matrix_;
    glm::mat4 projection_matrix_;
  };

}  // namespace engine
