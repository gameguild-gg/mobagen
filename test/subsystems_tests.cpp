#include <doctest/doctest.h>
#include "camera/camera.hpp"
#include "input/input_state.hpp"
#include "render/render_bridge.hpp"
#include "resource/resource_registry.hpp"

TEST_CASE("Camera: view matrix looks at target") {
  engine::Camera cam(engine::CameraMode::ORBIT);
  auto view = cam.get_view_matrix();
  auto pos = cam.get_position();
  auto eye_in_view = view * glm::vec4(pos, 1.0f);
  CHECK(eye_in_view.x == 0.0f);
  CHECK(eye_in_view.y == 0.0f);
  CHECK(eye_in_view.z == 0.0f);
}

TEST_CASE("InputState: key press and release round-trip") {
  input::InputState state;
  state.begin_frame();
  state.on_key(65, true);
  CHECK(state.pressed(65));
  CHECK(state.held(65));
  CHECK(!state.released(65));
  state.begin_frame();
  CHECK(!state.pressed(65));
  CHECK(state.held(65));
  state.on_key(65, false);
  CHECK(!state.held(65));
  CHECK(state.released(65));
  state.begin_frame();
  CHECK(!state.released(65));
}

TEST_CASE("RenderBridge: ECS scene with 3 entities produces 3 draw commands") {
  ecs::World w;
  for (int i = 0; i < 3; ++i) {
    auto e = w.create();
    w.add<scene::Transform>(e, glm::vec3(float(i), 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    w.add<render::VolumeRenderable>(e);
  }
  render::RenderBridge bridge;
  bridge.build(w);
  auto cmds = bridge.volume_commands();
  CHECK(cmds.size() == 3);
}

TEST_CASE("ResourceRegistry: create, get, release") {
  struct TestRes {
    int value = 0;
  };
  resource::ResourceRegistry<TestRes> reg;
  auto h = reg.create();
  auto* p = reg.get(h);
  REQUIRE(p != nullptr);
  CHECK(p->value == 0);
  auto h2 = reg.create(TestRes{42});
  auto* p2 = reg.get(h2);
  REQUIRE(p2 != nullptr);
  CHECK(p2->value == 42);
  reg.release(h2);
  auto* p3 = reg.get(h2);
  CHECK(p3 == nullptr);
}
