#include "execution_graph.hpp"

#include <utility>

namespace mobagen::modules {

  namespace {

    void add_issue(ExecutionGraphResult& result, ExecutionGraphIssueCode code, CapabilityIndex capability, ProviderIndex provider,
                   std::string message) {
      result.issues.push_back({code, capability, provider, std::move(message)});
    }

  }  // namespace

  ExecutionGraphResult freeze_execution_graph(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                              const ModuleActivation& activation) {
    ExecutionGraphResult result;
    if (registry.generation() != resolution.registry_generation() || registry.generation() != activation.registry_generation()) {
      add_issue(result, ExecutionGraphIssueCode::RegistryMismatch, {}, {}, "registry, resolution, and activation generations must match");
      return result;
    }
    if (activation.state() != ModuleLifecycleState::Active) {
      add_issue(result, ExecutionGraphIssueCode::InactiveActivation, {}, {}, "only an active module graph can be frozen");
      return result;
    }

    std::vector<ExecutionSlot> slots(registry.capability_count());
    std::size_t bound_slot_count = 0;
    for (const auto& selection : resolution.selections()) {
      const auto* provider = registry.provider(selection.provider);
      const auto* binding = activation.context().binding(selection.capability);
      if (provider == nullptr || registry.capability_name(selection.capability).empty() || binding == nullptr
          || binding->provider != selection.provider || binding->service == nullptr || selection.capability.value >= slots.size()) {
        add_issue(result, ExecutionGraphIssueCode::InvalidBinding, selection.capability, selection.provider,
                  "resolved capability is not bound by this activation");
        continue;
      }
      slots[selection.capability.value] = {.provider = selection.provider, .service = binding->service, .bound = true};
      ++bound_slot_count;
    }
    for (const auto& binding : activation.context().bindings()) {
      const auto* selection = resolution.selection_for(binding.capability);
      if (selection != nullptr && selection->provider == binding.provider) continue;
      add_issue(result, ExecutionGraphIssueCode::InvalidBinding, binding.capability, binding.provider,
                "activation contains a capability outside the resolved graph");
    }
    if (!result.issues.empty()) return result;

    result.graph = ExecutionGraph(activation.generation(), std::move(slots), bound_slot_count);
    return result;
  }

}  // namespace mobagen::modules
