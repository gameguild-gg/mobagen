#pragma once

#include "catalog_fetcher.hpp"
#include "catalog_index.hpp"
#include "resolver.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace mobagen::modules {

  struct ModuleSyncPlanResult {
    std::unique_ptr<ModuleCatalogIndex> catalog;
    std::optional<ModuleResolution> resolution;
    std::vector<CatalogFetchIssue> fetch_issues;
    std::vector<CatalogIndexIssue> catalog_issues;
    std::vector<ResolutionIssue> resolution_issues;

    [[nodiscard]] bool ok() const noexcept {
      return catalog != nullptr && resolution.has_value() && fetch_issues.empty() && catalog_issues.empty() && resolution_issues.empty();
    }
  };

  // Fetches catalog metadata and resolves providers. Selected artifact URLs are
  // retained in the plan but are never requested by this operation.
  [[nodiscard]] ModuleSyncPlanResult plan_module_sync(const ProductDescriptor& product, http::Client& client, const ResolverOptions& options);

}  // namespace mobagen::modules
