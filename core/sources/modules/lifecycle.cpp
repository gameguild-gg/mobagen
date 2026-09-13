#include "lifecycle.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <ranges>
#include <string_view>
#include <utility>

namespace mobagen::modules {

  namespace {

    std::atomic_uint64_t activation_generation_sequence{1};

    ActivationGeneration next_activation_generation() noexcept {
      auto value = activation_generation_sequence.fetch_add(1, std::memory_order_relaxed);
      if (value == 0) value = activation_generation_sequence.fetch_add(1, std::memory_order_relaxed);
      return {value};
    }

    void add_issue(std::vector<ModuleLifecycleIssue>& issues, ModuleLifecycleIssueCode code, ModuleLifecyclePhase phase, ProviderIndex provider,
                   std::string message) {
      issues.push_back({code, phase, provider, std::move(message)});
    }

    std::string callback_exception_message(std::string_view operation, const std::exception* exception) {
      std::string message = "module ";
      message += operation;
      message += " callback threw";
      if (exception != nullptr) {
        message += ": ";
        message += exception->what();
      }
      return message;
    }

    bool invoke_step(const ModuleLifecycleBinding& binding, ModuleLifecycleApi::Configure callback, ModuleLifecycleIssueCode failure_code,
                     ModuleLifecyclePhase phase, std::string_view operation, ModuleContext& context, std::vector<ModuleLifecycleIssue>& issues) {
      if (callback == nullptr) return true;
      try {
        if (callback(binding.api.state, context)) return true;
        add_issue(issues, failure_code, phase, binding.provider, "module " + std::string(operation) + " callback reported failure");
      } catch (const std::exception& exception) {
        add_issue(issues, ModuleLifecycleIssueCode::CallbackException, phase, binding.provider, callback_exception_message(operation, &exception));
      } catch (...) {
        add_issue(issues, ModuleLifecycleIssueCode::CallbackException, phase, binding.provider, callback_exception_message(operation, nullptr));
      }
      return false;
    }

    void invoke_action(const ModuleLifecycleBinding& binding, ModuleLifecycleApi::Action callback, ModuleLifecyclePhase phase,
                       std::string_view operation, ModuleContext& context, std::vector<ModuleLifecycleIssue>& issues) {
      if (callback == nullptr) return;
      try {
        callback(binding.api.state, context);
      } catch (const std::exception& exception) {
        add_issue(issues, ModuleLifecycleIssueCode::CallbackException, phase, binding.provider, callback_exception_message(operation, &exception));
      } catch (...) {
        add_issue(issues, ModuleLifecycleIssueCode::CallbackException, phase, binding.provider, callback_exception_message(operation, nullptr));
      }
    }

    void invoke_reverse(std::span<const ModuleLifecycleBinding> modules, ModuleLifecycleApi::Action ModuleLifecycleApi::*callback,
                        ModuleLifecyclePhase phase, std::string_view operation, ModuleContext& context, std::vector<ModuleLifecycleIssue>& issues) {
      for (auto module = modules.rbegin(); module != modules.rend(); ++module) {
        invoke_action(*module, module->api.*callback, phase, operation, context, issues);
      }
    }

    bool provider_supports(const CapabilityRegistry& registry, CapabilityIndex capability, ProviderIndex provider) {
      return std::ranges::find(registry.providers_for(capability), provider) != registry.providers_for(capability).end();
    }

    bool validate_context(const CapabilityRegistry& registry, const ModuleResolution& resolution, const ModuleContext& context,
                          std::vector<ModuleLifecycleIssue>& issues) {
      for (const auto& binding : context.bindings()) {
        const auto* selection = resolution.selection_for(binding.capability);
        if (registry.provider(binding.provider) != nullptr && !registry.capability_name(binding.capability).empty() && selection != nullptr
            && selection->provider == binding.provider && provider_supports(registry, binding.capability, binding.provider)) {
          continue;
        }
        add_issue(issues, ModuleLifecycleIssueCode::InvalidCapabilityBinding, ModuleLifecyclePhase::Validate, binding.provider,
                  "module context contains a capability binding outside the resolved graph");
      }
      for (const auto& selection : resolution.selections()) {
        const auto* binding = context.binding(selection.capability);
        if (binding != nullptr && binding->provider == selection.provider) continue;
        add_issue(issues, ModuleLifecycleIssueCode::InvalidCapabilityBinding, ModuleLifecyclePhase::Validate, selection.provider,
                  "resolved capability does not have a runtime binding");
      }
      return issues.empty();
    }

    std::vector<ModuleLifecycleBinding> order_bindings(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                                       std::span<const ModuleLifecycleBinding> bindings, std::vector<ModuleLifecycleIssue>& issues) {
      std::vector<ModuleLifecycleBinding> ordered;
      ordered.reserve(resolution.lifecycle_order().size());

      for (const auto& binding : bindings) {
        if (registry.provider(binding.provider) == nullptr) {
          add_issue(issues, ModuleLifecycleIssueCode::InvalidBindings, ModuleLifecyclePhase::Validate, binding.provider,
                    "lifecycle table contains an invalid provider index");
        }
      }
      for (const auto provider : resolution.lifecycle_order()) {
        const auto matches = std::ranges::count(bindings, provider, &ModuleLifecycleBinding::provider);
        if (matches != 1) {
          add_issue(issues, ModuleLifecycleIssueCode::InvalidBindings, ModuleLifecyclePhase::Validate, provider,
                    matches == 0 ? "resolved provider is missing from the lifecycle table"
                                 : "resolved provider appears more than once in the lifecycle table");
          continue;
        }
        ordered.push_back(*std::ranges::find(bindings, provider, &ModuleLifecycleBinding::provider));
      }
      for (const auto& binding : bindings) {
        if (std::ranges::find(resolution.lifecycle_order(), binding.provider) == resolution.lifecycle_order().end()) {
          add_issue(issues, ModuleLifecycleIssueCode::InvalidBindings, ModuleLifecyclePhase::Validate, binding.provider,
                    "lifecycle table contains a provider outside the resolved graph");
        }
      }
      return ordered;
    }

  }  // namespace

  bool ModuleContext::bind(CapabilityIndex capability, ProviderIndex provider, void* service) {
    if (service == nullptr) return false;
    const auto position
        = std::ranges::lower_bound(bindings_, capability.value, {}, [](const RuntimeCapabilityBinding& binding) { return binding.capability.value; });
    if (position != bindings_.end() && position->capability == capability) return false;
    bindings_.insert(position, {capability, provider, service});
    return true;
  }

  const RuntimeCapabilityBinding* ModuleContext::binding(CapabilityIndex capability) const noexcept {
    const auto found
        = std::ranges::lower_bound(bindings_, capability.value, {}, [](const RuntimeCapabilityBinding& binding) { return binding.capability.value; });
    return found == bindings_.end() || found->capability != capability ? nullptr : &*found;
  }

  std::span<const RuntimeCapabilityBinding> ModuleContext::bindings() const noexcept { return bindings_; }

  ModuleActivation::ModuleActivation(ModuleContext context, std::vector<ModuleLifecycleBinding> modules, ActivationGeneration generation)
      : context_(std::move(context)), modules_(std::move(modules)), generation_(generation) {}

  ModuleActivation::~ModuleActivation() {
    if (state_ == ModuleLifecycleState::Active) (void)quiesce();
    if (state_ == ModuleLifecycleState::Quiesced) (void)stop();
  }

  ModuleLifecycleState ModuleActivation::state() const noexcept { return state_; }

  ActivationGeneration ModuleActivation::generation() const noexcept { return generation_; }

  const ModuleContext& ModuleActivation::context() const noexcept { return context_; }

  ModuleLifecycleResult ModuleActivation::quiesce() {
    ModuleLifecycleResult result;
    if (state_ != ModuleLifecycleState::Active) {
      add_issue(result.issues, ModuleLifecycleIssueCode::InvalidTransition, ModuleLifecyclePhase::Quiesce, {},
                "only an active module graph can be quiesced");
      return result;
    }
    invoke_reverse(modules_, &ModuleLifecycleApi::quiesce, ModuleLifecyclePhase::Quiesce, "quiesce", context_, result.issues);
    state_ = ModuleLifecycleState::Quiesced;
    return result;
  }

  ModuleLifecycleResult ModuleActivation::stop() {
    ModuleLifecycleResult result;
    if (state_ != ModuleLifecycleState::Quiesced) {
      add_issue(result.issues, ModuleLifecycleIssueCode::InvalidTransition, ModuleLifecyclePhase::Stop, {},
                "only a quiesced module graph can be stopped");
      return result;
    }
    invoke_reverse(modules_, &ModuleLifecycleApi::stop, ModuleLifecyclePhase::Stop, "stop", context_, result.issues);
    state_ = ModuleLifecycleState::Stopped;
    return result;
  }

  ModuleActivationResult activate_modules(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                          std::span<const ModuleLifecycleBinding> bindings) {
    ModuleActivationResult result;
    auto ordered = order_bindings(registry, resolution, bindings, result.issues);
    if (!result.issues.empty()) return result;

    ModuleContext context;
    std::size_t configured = 0;
    for (const auto& binding : ordered) {
      if (!invoke_step(binding, binding.api.configure, ModuleLifecycleIssueCode::ConfigureFailed, ModuleLifecyclePhase::Configure, "configure",
                       context, result.issues)) {
        invoke_reverse(std::span(ordered).first(configured), &ModuleLifecycleApi::rollback, ModuleLifecyclePhase::Rollback, "rollback", context,
                       result.issues);
        return result;
      }
      ++configured;
    }
    if (!validate_context(registry, resolution, context, result.issues)) {
      invoke_reverse(std::span(ordered).first(configured), &ModuleLifecycleApi::rollback, ModuleLifecyclePhase::Rollback, "rollback", context,
                     result.issues);
      return result;
    }

    std::size_t started = 0;
    for (const auto& binding : ordered) {
      if (!invoke_step(binding, binding.api.start, ModuleLifecycleIssueCode::StartFailed, ModuleLifecyclePhase::Start, "start", context,
                       result.issues)) {
        const auto active = std::span(ordered).first(started);
        invoke_reverse(active, &ModuleLifecycleApi::quiesce, ModuleLifecyclePhase::Quiesce, "quiesce", context, result.issues);
        invoke_reverse(active, &ModuleLifecycleApi::stop, ModuleLifecyclePhase::Stop, "stop", context, result.issues);
        invoke_reverse(std::span(ordered).first(configured), &ModuleLifecycleApi::rollback, ModuleLifecyclePhase::Rollback, "rollback", context,
                       result.issues);
        return result;
      }
      ++started;
    }

    result.activation = std::unique_ptr<ModuleActivation>(new ModuleActivation(std::move(context), std::move(ordered), next_activation_generation()));
    return result;
  }

}  // namespace mobagen::modules
