#include "catalog_index.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace mobagen::modules {
  namespace {

    struct PendingArtifact {
      std::string provider_id;
      ModuleArtifactDescriptor artifact;
    };

    void add_issue(CatalogIndexResult& result, CatalogIndexIssueCode code, std::string provider_id, std::string field, std::string message,
                   std::vector<DescriptorIssue> descriptor_issues = {}, std::vector<RegistryIssue> registry_issues = {}) {
      result.issues.push_back({code, std::move(provider_id), std::move(field), std::move(message), std::move(descriptor_issues),
                               std::move(registry_issues)});
    }

    bool is_sha256(std::string_view value) {
      constexpr std::string_view prefix = "sha256:";
      return value.starts_with(prefix) && value.size() == prefix.size() + 64
             && std::ranges::all_of(value.substr(prefix.size()),
                                    [](char digit) { return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'); });
    }

    bool contains(std::span<const TargetPlatform> values, TargetPlatform target) {
      return std::ranges::find(values, target) != values.end();
    }

    bool contains(std::span<const LinkageMode> values, LinkageMode linkage) {
      return std::ranges::find(values, linkage) != values.end();
    }

    bool validate_artifacts(const PublishedProviderDescriptor& published, CatalogIndexResult& result) {
      const auto& provider = published.provider;
      bool valid = true;
      if (published.artifacts.empty()) {
        add_issue(result, CatalogIndexIssueCode::InvalidArtifact, provider.id, "artifacts", "provider must publish at least one artifact");
        return false;
      }
      if (published.artifacts.size() > max_module_provider_artifacts) {
        add_issue(result, CatalogIndexIssueCode::LimitExceeded, provider.id, "artifacts", "provider artifact count exceeds 1024");
        return false;
      }

      std::set<std::pair<TargetPlatform, LinkageMode>> seen;
      for (std::size_t index = 0; index < published.artifacts.size(); ++index) {
        const auto& artifact = published.artifacts[index];
        const std::string field = "artifacts[" + std::to_string(index) + ']';
        if (!is_secure_https_url(artifact.url)) {
          add_issue(result, CatalogIndexIssueCode::InvalidArtifact, provider.id, field + ".url", "artifact URL violates HTTPS policy");
          valid = false;
        }
        if (artifact.size == 0 || artifact.size > max_module_artifact_bytes) {
          add_issue(result, CatalogIndexIssueCode::InvalidArtifact, provider.id, field + ".size", "artifact size is outside the 512 MiB limit");
          valid = false;
        }
        if (!is_sha256(artifact.hash)) {
          add_issue(result, CatalogIndexIssueCode::InvalidArtifact, provider.id, field + ".hash", "artifact hash is not canonical SHA-256");
          valid = false;
        }
        if (!contains(provider.targets, artifact.target) || !contains(provider.linkages, artifact.linkage)) {
          add_issue(result, CatalogIndexIssueCode::InvalidArtifact, provider.id, field,
                    "artifact target and linkage must be declared by its provider descriptor");
          valid = false;
        }
        if (!seen.emplace(artifact.target, artifact.linkage).second) {
          add_issue(result, CatalogIndexIssueCode::DuplicateArtifact, provider.id, field,
                    "provider artifacts must have unique target and linkage pairs");
          valid = false;
        }
      }
      return valid;
    }

  }  // namespace

  ModuleCatalogIndex::ModuleCatalogIndex(CapabilityRegistry registry, std::vector<ModuleArtifactDescriptor> artifacts)
      : registry_(std::move(registry)), artifacts_(std::move(artifacts)) {}

  const ModuleArtifactDescriptor* ModuleCatalogIndex::artifact_for(ProviderIndex provider) const noexcept {
    return provider.value < artifacts_.size() ? &artifacts_[provider.value] : nullptr;
  }

  CatalogIndexResult build_module_catalog_index(std::span<const ModuleCatalogDescriptor> catalogs, TargetPlatform target, LinkageMode linkage) {
    CatalogIndexResult result;
    CapabilityRegistryBuilder registry_builder;
    std::vector<PendingArtifact> pending;

    for (std::size_t catalog_index = 0; catalog_index < catalogs.size(); ++catalog_index) {
      const auto& catalog = catalogs[catalog_index];
      if (catalog.schema != module_catalog_schema_version) {
        add_issue(result, CatalogIndexIssueCode::UnsupportedSchema, {}, "catalogs[" + std::to_string(catalog_index) + "].schema",
                  "only module catalog schema version 1 is supported");
        continue;
      }
      if (catalog.providers.size() > max_module_catalog_providers) {
        add_issue(result, CatalogIndexIssueCode::LimitExceeded, {}, "catalogs[" + std::to_string(catalog_index) + "].providers",
                  "catalog provider count exceeds 1024");
        continue;
      }

      for (const auto& published : catalog.providers) {
        auto descriptor_issues = validate(published.provider);
        if (!descriptor_issues.empty()) {
          add_issue(result, CatalogIndexIssueCode::InvalidProvider, published.provider.id, {}, "published provider descriptor is invalid",
                    std::move(descriptor_issues));
          continue;
        }
        if (!validate_artifacts(published, result)) continue;

        const auto artifact = std::ranges::find_if(published.artifacts, [=](const ModuleArtifactDescriptor& candidate) {
          return candidate.target == target && candidate.linkage == linkage;
        });
        if (artifact == published.artifacts.end()) continue;
        registry_builder.add(published.provider);
        pending.push_back({published.provider.id, *artifact});
      }
    }
    if (!result.issues.empty()) return result;

    auto registry = registry_builder.build();
    if (!registry.ok()) {
      add_issue(result, CatalogIndexIssueCode::RegistryFailed, {}, {}, "published providers do not form a valid capability registry", {},
                std::move(registry.issues));
      return result;
    }

    std::vector<ModuleArtifactDescriptor> artifacts(registry.registry->provider_count());
    for (auto& pending_artifact : pending) {
      const auto provider = registry.registry->find_provider(pending_artifact.provider_id);
      if (!provider.has_value()) {
        add_issue(result, CatalogIndexIssueCode::RegistryFailed, pending_artifact.provider_id, {},
                  "published provider disappeared while indexing the capability registry");
        return result;
      }
      artifacts[provider->value] = std::move(pending_artifact.artifact);
    }

    result.index = std::unique_ptr<ModuleCatalogIndex>(new ModuleCatalogIndex(std::move(*registry.registry), std::move(artifacts)));
    return result;
  }

}  // namespace mobagen::modules
