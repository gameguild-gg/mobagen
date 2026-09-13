#include "benchmark_runner.hpp"

#include "jobs/scheduler.hpp"
#include "render/render_bridge.hpp"
#include "scene/transform.hpp"
#include "world.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace {
  constexpr std::size_t kEntityCount = 200'000;
  constexpr std::size_t kRenderableCount = 10'000;
  constexpr std::size_t kJobGrain = 1'024;
  constexpr std::size_t kStartupEntityCount = 1'024;

  std::atomic<std::uint64_t> observation{0};

  struct Position {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  struct Velocity {
    float x = 1.0f;
    float y = 0.0f;
    float z = 0.0f;
  };

  unsigned benchmark_worker_count() { return std::max(1u, std::thread::hardware_concurrency()); }

  void construct_foundation_state() {
    ecs::World world;
    jobs::Scheduler scheduler{1, jobs::Scheduler::Mode::Inline};
    render::RenderBridge bridge;
    for (std::size_t index = 0; index < kStartupEntityCount; ++index) {
      const ecs::Entity entity = world.create();
      world.add<Position>(entity, Position{static_cast<float>(index), 0.0f, 0.0f});
      world.add<Velocity>(entity, Velocity{});
      if (index < 64) {
        world.add<scene::Transform>(entity);
        world.add<render::VolumeRenderable>(entity);
      }
    }
    bridge.build(world);
    observation.fetch_xor(static_cast<std::uint64_t>(world.alive() + bridge.volume_commands().size()), std::memory_order_relaxed);
  }

  class FoundationFixture {
  public:
    FoundationFixture() : scheduler_(benchmark_worker_count(), jobs::Scheduler::Mode::Threaded), job_values_(kEntityCount, 0) {
      for (std::size_t index = 0; index < kEntityCount; ++index) {
        const ecs::Entity entity = world_.create();
        world_.add<Position>(entity, Position{static_cast<float>(index), 0.0f, 0.0f});
        world_.add<Velocity>(entity, Velocity{});
        if (index < kRenderableCount) {
          world_.add<scene::Transform>(entity);
          world_.add<render::VolumeRenderable>(entity);
        }
        if (index == 0) first_entity_ = entity;
      }

      bridge_.build(world_);
      update_positions_impl();
      run_parallel_chunks_impl();
      bridge_.build(world_);
    }

    void update_positions() {
      update_positions_impl();
      observation.fetch_xor(static_cast<std::uint64_t>(world_.get<Position>(first_entity_).x), std::memory_order_relaxed);
    }

    void run_parallel_chunks() {
      run_parallel_chunks_impl();
      observation.fetch_xor(static_cast<std::uint64_t>(job_values_.front() + job_values_.back()), std::memory_order_relaxed);
    }

    void build_render_commands() {
      bridge_.build(world_);
      observation.fetch_xor(static_cast<std::uint64_t>(bridge_.volume_commands().size()), std::memory_order_relaxed);
    }

    void run_representative_frame() {
      update_positions_impl();
      bridge_.build(world_);
      observation.fetch_xor(
          static_cast<std::uint64_t>(world_.get<Position>(first_entity_).x) + static_cast<std::uint64_t>(bridge_.volume_commands().size()),
          std::memory_order_relaxed);
    }

  private:
    void update_positions_impl() {
      world_.view<Position, Velocity>([](ecs::Entity, Position& position, Velocity& velocity) {
        position.x += velocity.x;
        position.y += velocity.y;
        position.z += velocity.z;
      });
    }

    void run_parallel_chunks_impl() {
      jobs::WaitGroup completion;
      scheduler_.parallel_for(
          job_values_.size(), kJobGrain,
          [this](std::size_t begin, std::size_t end) {
            for (std::size_t index = begin; index < end; ++index) ++job_values_[index];
          },
          completion);
      scheduler_.wait(completion);
    }

    ecs::World world_;
    jobs::Scheduler scheduler_;
    render::RenderBridge bridge_;
    std::vector<int> job_values_;
    ecs::Entity first_entity_ = ecs::kInvalidEntity;
  };
}  // namespace

int main(int argc, char** argv) {
  try {
    const mobagen::benchmark::Options options = mobagen::benchmark::parse_options(argc, argv);
    FoundationFixture fixture;

    const std::array results{
        mobagen::benchmark::measure("startup.foundation", options, construct_foundation_state),
        mobagen::benchmark::measure("ecs.serial_update", options, [&fixture] { fixture.update_positions(); }),
        mobagen::benchmark::measure("jobs.parallel_for", options, [&fixture] { fixture.run_parallel_chunks(); }),
        mobagen::benchmark::measure("render.bridge_build", options, [&fixture] { fixture.build_render_commands(); }),
        mobagen::benchmark::measure("frame.foundation", options, [&fixture] { fixture.run_representative_frame(); }),
    };
    mobagen::benchmark::write_json(std::cout, options, results);
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "invalid benchmark arguments: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
