#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/app.hpp"
#include "modules/execution_graph.hpp"

namespace mobagen::compositions {

  enum class GraphicsCompositionIssueCode : std::uint8_t { HostResources, Registry, Resolution, Activation, Freeze, Runtime, Shutdown };

  struct GraphicsCompositionIssue {
    GraphicsCompositionIssueCode code{};
    std::string message;
  };

  class GraphicsComposition;

  struct GraphicsCompositionResult {
    std::unique_ptr<GraphicsComposition> composition;
    std::vector<GraphicsCompositionIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return composition != nullptr; }
  };

  [[nodiscard]] modules::ProductDescriptor default_graphics_product();

  class GraphicsComposition {
  public:
    GraphicsComposition(const GraphicsComposition&) = delete;
    GraphicsComposition& operator=(const GraphicsComposition&) = delete;
    GraphicsComposition(GraphicsComposition&&) = delete;
    GraphicsComposition& operator=(GraphicsComposition&&) = delete;
    ~GraphicsComposition();

    [[nodiscard]] bool tick(float dt) noexcept;
    [[nodiscard]] bool draw(WGPURenderPassEncoder pass) noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    [[nodiscard]] std::uint64_t draws() const noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool stopped() const noexcept;
    [[nodiscard]] bool resources_alive_during_shutdown() const noexcept;
    [[nodiscard]] modules::ModuleLifecycleResult stop();
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept;
    [[nodiscard]] const modules::ModuleResolution& resolution() const noexcept;
    [[nodiscard]] const std::vector<std::string>& lifecycle_trace() const noexcept;

  private:
    friend GraphicsCompositionResult create_graphics_composition(app::App&, const modules::ProductDescriptor&, modules::TargetPlatform,
                                                                 std::string_view);

    struct Impl;
    explicit GraphicsComposition(std::unique_ptr<Impl> implementation);

    std::unique_ptr<Impl> impl_;
  };

  [[nodiscard]] GraphicsCompositionResult create_graphics_composition(app::App& host, const modules::ProductDescriptor& product,
                                                                      modules::TargetPlatform target, std::string_view profile);

  class GraphicsAppCallbacks final : public app::AppCallbacks {
  public:
    explicit GraphicsAppCallbacks(std::uint64_t exit_after_frames = 0);

    SDL_AppResult on_init(app::App& host, int argc, char** argv) override;
    SDL_AppResult on_ready(app::App& host) override;
    SDL_AppResult on_iterate(app::App& host, float dt) override;
    void on_draw(app::App& host, WGPURenderPassEncoder pass) override;
    void on_shutdown(app::App& host) override;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool stopped() const noexcept;
    [[nodiscard]] bool resources_alive_during_shutdown() const noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    [[nodiscard]] std::uint64_t draws() const noexcept;
    [[nodiscard]] const modules::CapabilityRegistry* registry() const noexcept;
    [[nodiscard]] const modules::ModuleResolution* resolution() const noexcept;
    [[nodiscard]] const std::vector<std::string>& lifecycle_trace() const noexcept;
    [[nodiscard]] std::span<const GraphicsCompositionIssue> issues() const noexcept;

  private:
    modules::ProductDescriptor product_;
    std::unique_ptr<GraphicsComposition> composition_;
    std::vector<GraphicsCompositionIssue> issues_;
    std::uint64_t exit_after_frames_{};
    bool runtime_failed_{};
  };

}  // namespace mobagen::compositions
