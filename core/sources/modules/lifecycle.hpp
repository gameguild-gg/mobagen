#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "resolver.hpp"

namespace mobagen::modules {

  struct RuntimeCapabilityBinding {
    CapabilityIndex capability{};
    ProviderIndex provider{};
    void* service{};
  };

  class ModuleContext {
  public:
    [[nodiscard]] bool bind(CapabilityIndex capability, ProviderIndex provider, void* service);
    [[nodiscard]] const RuntimeCapabilityBinding* binding(CapabilityIndex capability) const noexcept;
    [[nodiscard]] std::span<const RuntimeCapabilityBinding> bindings() const noexcept;

  private:
    std::vector<RuntimeCapabilityBinding> bindings_;
  };

  struct ModuleLifecycleApi {
    using Configure = bool (*)(void* state, ModuleContext& context);
    using Start = bool (*)(void* state, ModuleContext& context);
    using Action = void (*)(void* state, ModuleContext& context);

    void* state{};
    Configure configure{};
    Start start{};
    Action quiesce{};
    Action stop{};
    Action rollback{};
  };

  struct ModuleLifecycleBinding {
    ProviderIndex provider{};
    ModuleLifecycleApi api;
  };

  struct ActivationGeneration {
    std::uint64_t value{};

    friend bool operator==(const ActivationGeneration&, const ActivationGeneration&) = default;
  };

  enum class ModuleLifecycleState : std::uint8_t { Active, Quiesced, Stopped };

  enum class ModuleLifecyclePhase : std::uint8_t { Validate, Configure, Start, Quiesce, Stop, Rollback };

  enum class ModuleLifecycleIssueCode : std::uint8_t {
    InvalidBindings,
    InvalidCapabilityBinding,
    ConfigureFailed,
    StartFailed,
    CallbackException,
    InvalidTransition,
  };

  struct ModuleLifecycleIssue {
    ModuleLifecycleIssueCode code{};
    ModuleLifecyclePhase phase{};
    ProviderIndex provider{};
    std::string message;
  };

  struct ModuleLifecycleResult {
    std::vector<ModuleLifecycleIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
  };

  struct ModuleActivationResult;

  class ModuleActivation {
  public:
    ModuleActivation(const ModuleActivation&) = delete;
    ModuleActivation& operator=(const ModuleActivation&) = delete;
    ModuleActivation(ModuleActivation&&) = delete;
    ModuleActivation& operator=(ModuleActivation&&) = delete;
    ~ModuleActivation();

    [[nodiscard]] ModuleLifecycleState state() const noexcept;
    [[nodiscard]] RegistryGeneration registry_generation() const noexcept { return registry_generation_; }
    [[nodiscard]] ActivationGeneration generation() const noexcept;
    [[nodiscard]] const ModuleContext& context() const noexcept;
    [[nodiscard]] ModuleLifecycleResult quiesce();
    [[nodiscard]] ModuleLifecycleResult stop();

  private:
    friend ModuleActivationResult activate_modules(const CapabilityRegistry&, const ModuleResolution&, std::span<const ModuleLifecycleBinding>);

    ModuleActivation(ModuleContext context, std::vector<ModuleLifecycleBinding> modules, RegistryGeneration registry_generation,
                     ActivationGeneration generation);

    ModuleContext context_;
    std::vector<ModuleLifecycleBinding> modules_;
    RegistryGeneration registry_generation_;
    ActivationGeneration generation_;
    ModuleLifecycleState state_{ModuleLifecycleState::Active};
  };

  struct ModuleActivationResult {
    std::unique_ptr<ModuleActivation> activation;
    std::vector<ModuleLifecycleIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return activation != nullptr; }
  };

  [[nodiscard]] ModuleActivationResult activate_modules(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                                        std::span<const ModuleLifecycleBinding> bindings);

}  // namespace mobagen::modules
