#pragma once

#include "lockfile_verifier.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mobagen::modules {

  enum class LockedActivationPlanIssueCode : std::uint8_t {
    InvalidSelection,
    MissingVerifiedPlugin,
    UnexpectedVerifiedPlugin,
    MetadataMismatch,
    InvalidDependency,
    DependencyCycle,
  };

  struct LockedActivationPlanIssue {
    LockedActivationPlanIssueCode code{};
    std::string provider_id;
    std::string message;
  };

  struct LockedPluginDependency {
    std::string provider_id;
    std::string capability;
  };

  struct LockedPluginActivationEntry {
    std::string provider_id;
    SemanticVersion version;
    LinkageMode linkage{};
    std::uint32_t abi_version{};
    std::uint64_t size{};
    std::filesystem::path package_path;
    std::filesystem::path binary_path;
    std::vector<std::string> capabilities;
    std::vector<LockedPluginDependency> dependencies;
    std::string configuration_schema;
    std::string configuration_hash;
  };

  class LockedPluginActivationPlan {
  public:
    [[nodiscard]] std::span<const LockedPluginActivationEntry> entries() const noexcept {
      return entries_;
    }
    [[nodiscard]] const LockedPluginActivationEntry* find(
        std::string_view provider_id
    ) const noexcept;

  private:
    friend struct LockedActivationPlanResult;
    friend LockedActivationPlanResult build_locked_plugin_activation_plan(
        const LockfileDocument&, std::span<const VerifiedLockedPlugin>
    );

    explicit LockedPluginActivationPlan(std::vector<LockedPluginActivationEntry> entries)
        : entries_(std::move(entries)) {}

    std::vector<LockedPluginActivationEntry> entries_;
  };

  struct LockedActivationPlanResult {
    std::unique_ptr<LockedPluginActivationPlan> plan;
    std::vector<LockedActivationPlanIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return plan != nullptr && issues.empty(); }
  };

  /* Builds a deterministic lifecycle plan from already-verified metadata only. */
  [[nodiscard]] LockedActivationPlanResult build_locked_plugin_activation_plan(
      const LockfileDocument& document, std::span<const VerifiedLockedPlugin> verified_plugins
  );

}  // namespace mobagen::modules
