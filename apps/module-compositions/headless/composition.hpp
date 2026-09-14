#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "modules/execution_graph.hpp"

namespace mobagen::compositions {

  enum class HeadlessCompositionIssueCode : std::uint8_t { Registry, Resolution, Activation, Freeze };

  struct HeadlessCompositionIssue {
    HeadlessCompositionIssueCode code{};
    std::string message;
  };

  class HeadlessComposition;

  struct HeadlessCompositionResult {
    std::unique_ptr<HeadlessComposition> composition;
    std::vector<HeadlessCompositionIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return composition != nullptr; }
  };

  class HeadlessComposition {
  public:
    HeadlessComposition(const HeadlessComposition&) = delete;
    HeadlessComposition& operator=(const HeadlessComposition&) = delete;
    HeadlessComposition(HeadlessComposition&&) = delete;
    HeadlessComposition& operator=(HeadlessComposition&&) = delete;
    ~HeadlessComposition();

    [[nodiscard]] bool tick() noexcept;
    [[nodiscard]] std::uint64_t ticks() const noexcept;
    [[nodiscard]] modules::ModuleLifecycleResult stop();
    [[nodiscard]] const modules::CapabilityRegistry& registry() const noexcept;
    [[nodiscard]] const modules::ModuleResolution& resolution() const noexcept;

  private:
    friend HeadlessCompositionResult create_headless_composition(const modules::ProductDescriptor&, modules::TargetPlatform, std::string_view);

    struct TickService {
      std::uint64_t ticks{};
    };

    struct RuntimeState {
      modules::ProviderIndex provider;
      modules::CapabilityIndex capability;
      TickService service;
    };

    HeadlessComposition(modules::CapabilityRegistry registry, modules::ModuleResolution resolution);
    static bool configure(void* state, modules::ModuleContext& context);

    modules::CapabilityRegistry registry_;
    modules::ModuleResolution resolution_;
    RuntimeState runtime_;
    modules::ModuleActivationResult activation_;
    modules::ExecutionGraphResult graph_;
    std::optional<modules::CapabilityHandle<TickService>> tick_handle_;
  };

  [[nodiscard]] modules::TargetPlatform native_target_platform() noexcept;
  [[nodiscard]] HeadlessCompositionResult create_headless_composition(const modules::ProductDescriptor& product, modules::TargetPlatform target,
                                                                      std::string_view profile);

}  // namespace mobagen::compositions
