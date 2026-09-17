#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "lifecycle.hpp"

namespace mobagen::modules {

  struct ExecutionSlot {
    ProviderIndex provider{};
    void* service{};
    bool bound{};
  };

  class ExecutionGraph;

  template <typename Service> class CapabilityHandle {
  public:
    CapabilityHandle() = default;

  private:
    friend class ExecutionGraph;

    CapabilityHandle(CapabilityIndex capability, ActivationGeneration generation) : capability_(capability), generation_(generation) {}

    CapabilityIndex capability_{};
    ActivationGeneration generation_{};
  };

  enum class ExecutionGraphIssueCode : std::uint8_t { InactiveActivation, RegistryMismatch, InvalidBinding };

  struct ExecutionGraphIssue {
    ExecutionGraphIssueCode code{};
    CapabilityIndex capability{};
    ProviderIndex provider{};
    std::string message;
  };

  struct ExecutionGraphResult;

  class ExecutionGraph {
  public:
    ExecutionGraph(const ExecutionGraph&) = delete;
    ExecutionGraph& operator=(const ExecutionGraph&) = delete;
    ExecutionGraph(ExecutionGraph&&) noexcept = default;
    ExecutionGraph& operator=(ExecutionGraph&&) noexcept = default;

    [[nodiscard]] ActivationGeneration generation() const noexcept { return generation_; }
    [[nodiscard]] std::span<const ExecutionSlot> slots() const noexcept { return slots_; }
    [[nodiscard]] std::size_t bound_slot_count() const noexcept { return bound_slot_count_; }

    template <typename Service> [[nodiscard]] std::optional<CapabilityHandle<Service>> handle(CapabilityIndex capability) const noexcept {
      static_assert(!std::is_void_v<Service>);
      if (capability.value >= slots_.size() || !slots_[capability.value].bound) return std::nullopt;
      return CapabilityHandle<Service>{capability, generation_};
    }

    template <typename Service> [[nodiscard]] Service* service(CapabilityHandle<Service> handle) const noexcept {
      static_assert(!std::is_void_v<Service>);
      if (handle.generation_ != generation_ || handle.capability_.value >= slots_.size()) return nullptr;
      const auto& slot = slots_[handle.capability_.value];
      return slot.bound ? static_cast<Service*>(slot.service) : nullptr;
    }

  private:
    friend ExecutionGraphResult freeze_execution_graph(const CapabilityRegistry&, const ModuleResolution&, const ModuleActivation&);

    ExecutionGraph(ActivationGeneration generation, std::vector<ExecutionSlot> slots, std::size_t bound_slot_count)
        : generation_(generation), slots_(std::move(slots)), bound_slot_count_(bound_slot_count) {}

    ActivationGeneration generation_;
    std::vector<ExecutionSlot> slots_;
    std::size_t bound_slot_count_{};
  };

  struct ExecutionGraphResult {
    std::optional<ExecutionGraph> graph;
    std::vector<ExecutionGraphIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return graph.has_value(); }
  };

  [[nodiscard]] ExecutionGraphResult freeze_execution_graph(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                                            const ModuleActivation& activation);

}  // namespace mobagen::modules
