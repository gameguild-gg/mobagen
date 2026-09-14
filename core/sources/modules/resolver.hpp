#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "capability_registry.hpp"

namespace mobagen::modules {

  struct ModuleAliasBinding {
    std::string alias;
    std::string capability;
  };

  struct DefaultProviderBinding {
    TargetPlatform target{};
    std::string profile;
    std::string capability;
    std::string provider;
  };

  struct ResolverOptions {
    TargetPlatform target{};
    std::string profile;
    std::vector<ModuleAliasBinding> aliases;
    std::vector<DefaultProviderBinding> defaults;
  };

  enum class ResolutionIssueCode : std::uint8_t {
    InvalidProduct,
    UnknownProfile,
    DuplicateAlias,
    UnknownAlias,
    DuplicateDefault,
    MissingDefault,
    UnknownProvider,
    MissingCapability,
    ProviderDoesNotProvide,
    UnsupportedTarget,
    UnsupportedLinkage,
    ConflictingSelection,
    AmbiguousProvider,
    PermissionDenied,
    ProviderConflict,
    DependencyCycle,
  };

  struct ResolutionIssue {
    ResolutionIssueCode code{};
    std::string module_alias;
    std::string capability;
    std::string provider_id;
    std::string message;
    std::vector<DescriptorIssue> descriptor_issues;
  };

  struct ResolvedCapability {
    CapabilityIndex capability{};
    ProviderIndex provider{};
    LinkageMode linkage{LinkageMode::Static};
    std::string reason;
  };

  struct ProviderDependency {
    ProviderIndex dependency{};
    ProviderIndex dependent{};
    CapabilityIndex capability{};

    friend bool operator==(const ProviderDependency&, const ProviderDependency&) = default;
  };

  class ModuleResolution {
  public:
    [[nodiscard]] RegistryGeneration registry_generation() const noexcept { return registry_generation_; }
    [[nodiscard]] const ResolvedCapability* selection_for(CapabilityIndex capability) const noexcept;
    [[nodiscard]] std::span<const ResolvedCapability> selections() const noexcept;
    [[nodiscard]] std::span<const ProviderDependency> dependencies() const noexcept;
    [[nodiscard]] std::span<const ProviderIndex> lifecycle_order() const noexcept;

  private:
    friend struct ModuleResolutionBuilder;

    RegistryGeneration registry_generation_;
    std::vector<ResolvedCapability> selections_;
    std::vector<ProviderDependency> dependencies_;
    std::vector<ProviderIndex> lifecycle_order_;
  };

  struct ResolutionResult {
    std::optional<ModuleResolution> resolution;
    std::vector<ResolutionIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return resolution.has_value(); }
  };

  [[nodiscard]] ResolutionResult resolve_modules(const ProductDescriptor& product, const CapabilityRegistry& registry,
                                                 const ResolverOptions& options);

}  // namespace mobagen::modules
