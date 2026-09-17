#include "module_sync_plan.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace mobagen::modules {

  ModuleSyncPlanResult plan_module_sync(const ProductDescriptor& product, http::Client& client, const ResolverOptions& options) {
    ModuleSyncPlanResult result;

    auto descriptor_issues = validate(product);
    if (!descriptor_issues.empty()) {
      result.fetch_issues.push_back({
          .code = CatalogFetchIssueCode::InvalidProduct,
          .message = "product descriptor is invalid",
          .descriptor_issues = std::move(descriptor_issues),
      });
      return result;
    }

    const auto profile = std::ranges::find(product.profiles, options.profile, &ProfileDescriptor::name);
    if (profile == product.profiles.end()) {
      result.resolution_issues.push_back({
          .code = ResolutionIssueCode::UnknownProfile,
          .message = "active profile is not declared by the product",
      });
      return result;
    }

    auto fetched = fetch_module_catalogs(product, client);
    if (!fetched.ok()) {
      result.fetch_issues = std::move(fetched.issues);
      return result;
    }

    std::vector<ModuleCatalogDescriptor> catalogs;
    catalogs.reserve(fetched.catalogs.size());
    for (auto& fetched_catalog : fetched.catalogs) catalogs.push_back(std::move(fetched_catalog.catalog));

    auto indexed = build_module_catalog_index(catalogs, options.target, profile->linkage);
    if (!indexed.ok()) {
      result.catalog_issues = std::move(indexed.issues);
      return result;
    }

    auto resolved = resolve_modules(product, indexed.index->registry(), options);
    if (!resolved.ok()) {
      result.resolution_issues = std::move(resolved.issues);
      return result;
    }

    result.catalog = std::move(indexed.index);
    result.resolution = std::move(resolved.resolution);
    return result;
  }

}  // namespace mobagen::modules
