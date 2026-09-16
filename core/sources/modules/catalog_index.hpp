#pragma once

#include "capability_registry.hpp"
#include "catalog.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mobagen::modules {

  enum class CatalogIndexIssueCode : std::uint8_t {
    UnsupportedSchema,
    LimitExceeded,
    InvalidProvider,
    InvalidArtifact,
    DuplicateArtifact,
    RegistryFailed,
  };

  struct CatalogIndexIssue {
    CatalogIndexIssueCode code{};
    std::string provider_id;
    std::string field;
    std::string message;
    std::vector<DescriptorIssue> descriptor_issues;
    std::vector<RegistryIssue> registry_issues;
  };

  class ModuleCatalogIndex;

  struct CatalogIndexResult {
    std::unique_ptr<ModuleCatalogIndex> index;
    std::vector<CatalogIndexIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return index != nullptr && issues.empty(); }
  };

  class ModuleCatalogIndex {
  public:
    [[nodiscard]] const CapabilityRegistry& registry() const noexcept { return registry_; }
    [[nodiscard]] const ModuleArtifactDescriptor* artifact_for(ProviderIndex provider) const noexcept;

  private:
    friend CatalogIndexResult build_module_catalog_index(std::span<const ModuleCatalogDescriptor>, TargetPlatform, LinkageMode);

    ModuleCatalogIndex(CapabilityRegistry registry, std::vector<ModuleArtifactDescriptor> artifacts);

    CapabilityRegistry registry_;
    std::vector<ModuleArtifactDescriptor> artifacts_;
  };

  [[nodiscard]] CatalogIndexResult build_module_catalog_index(std::span<const ModuleCatalogDescriptor> catalogs, TargetPlatform target,
                                                              LinkageMode linkage);

}  // namespace mobagen::modules
