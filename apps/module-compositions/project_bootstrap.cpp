#include "project_bootstrap.hpp"

#include "assets/asset_cache.hpp"
#include "modules/lockfile.hpp"
#include "modules/lockfile_verifier.hpp"
#include "modules/manifest_parser.hpp"
#include "modules/module_sync_plan.hpp"
#include "project_support.hpp"

#include <algorithm>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace mobagen::compositions {
  namespace {

    struct BootstrapProbe {
      bool ready{};
      std::size_t plugin_count{};
      std::size_t selected_count{};
      std::string reason;
    };

    void fail(
        ProjectBootstrapResult& result, ProjectBootstrapIssueCode code,
        std::string message
    ) {
      result.issues.push_back({.code = code, .message = std::move(message)});
    }

    bool lock_matches_request(
        const ProjectBootstrapOptions& options,
        const modules::ProductDescriptor& product,
        modules::LinkageMode profile_linkage,
        const modules::LockfileDocument& document
    ) {
      std::set<std::string, std::less<>> aliases;
      for (const auto& alias : options.resolver.aliases) {
        if (!aliases.insert(alias.alias).second) return false;
      }
      std::set<std::string, std::less<>> default_capabilities;
      for (const auto& binding : options.resolver.defaults) {
        if (!default_capabilities.insert(binding.capability).second) return false;
        const auto selection = std::ranges::find(
            document.resolved, binding.capability,
            &modules::LockedProviderSelection::capability
        );
        if (selection == document.resolved.end()
            || selection->provider != binding.provider) {
          return false;
        }
      }
      for (const auto& request : product.modules) {
        const auto alias = std::ranges::find(
            options.resolver.aliases, request.alias,
            &modules::ModuleAliasBinding::alias
        );
        if (!request.capability.empty()
            && alias != options.resolver.aliases.end()
            && alias->capability != request.capability) {
          return false;
        }
        const std::string_view capability = request.capability.empty()
                                                ? alias
                                                          == options.resolver.aliases.end()
                                                      ? std::string_view{}
                                                      : std::string_view{alias->capability}
                                                : std::string_view{request.capability};
        if (capability.empty()) return false;
        const auto selection = std::ranges::find(
            document.resolved, capability,
            &modules::LockedProviderSelection::capability
        );
        if (selection == document.resolved.end()
            || (request.provider != "default"
                && selection->provider != request.provider)) {
          return false;
        }
      }

      std::set<std::string, std::less<>> plugin_providers;
      for (const auto& plugin : document.metadata.plugins) {
        plugin_providers.insert(plugin.provider);
      }
      std::set<std::string, std::less<>> selected_providers;
      for (const auto& selection : document.resolved) {
        if (selection.linkage != profile_linkage) return false;
        selected_providers.insert(selection.provider);
      }
      return !selected_providers.empty()
             && selected_providers == plugin_providers;
    }

    BootstrapProbe probe_bootstrap(
        const ProjectBootstrapOptions& options,
        const modules::ProductDescriptor& product,
        modules::LinkageMode profile_linkage,
        const std::filesystem::path& manifest_path,
        std::string_view manifest_hash
    ) {
      const auto lockfile_path = manifest_path.parent_path() / "mobagen.lock";
      const auto source = modules::read_lockfile_bounded(lockfile_path);
      if (!source.ok()) {
        return {.reason = "mobagen.lock is missing or unreadable"};
      }
      const auto parsed = modules::parse_lockfile(
          *source.contents, lockfile_path.generic_string()
      );
      if (!parsed.ok()) return {.reason = "mobagen.lock is invalid"};
      const modules::LockfileVerificationContext context{
          .sdk = options.sdk_version,
          .target = options.resolver.target,
          .profile = options.resolver.profile,
          .manifest_hash = std::string{manifest_hash},
      };
      std::size_t plugin_count = 0;
      if (options.validation == ProjectBootstrapValidation::Full) {
        const auto verified = modules::verify_locked_project(
            *parsed.document, manifest_path.parent_path(), context
        );
        if (!verified.ok()) {
          return {
              .reason = verified.issues.empty()
                            ? "locked plugin packages are invalid"
                            : verified.issues.front().message,
          };
        }
        plugin_count = verified.plugins.size();
      } else {
        const auto inspected = modules::inspect_locked_project(
            *parsed.document, manifest_path.parent_path(), context
        );
        if (!inspected.ok()) {
          return {
              .reason = inspected.issues.empty()
                            ? "locked plugin packages are invalid"
                            : inspected.issues.front().message,
          };
        }
        plugin_count = inspected.plugins.size();
      }
      if (!lock_matches_request(
              options, product, profile_linkage, *parsed.document
          )) {
        return {
            .reason = "mobagen.lock does not match the requested module selection",
        };
      }
      return {
          .ready = true,
          .plugin_count = plugin_count,
          .selected_count = parsed.document->resolved.size(),
      };
    }

    std::string sync_plan_failure(const modules::ModuleSyncPlanResult& planned) {
      std::string message{"module synchronization could not be planned"};
      if (!planned.fetch_issues.empty()) {
        message += ": " + planned.fetch_issues.front().message;
        if (!planned.fetch_issues.front().catalog_errors.empty()) {
          message += ": "
                     + planned.fetch_issues.front().catalog_errors.front().message;
        } else if (!planned.fetch_issues.front().transport_error.message.empty()) {
          message += ": " + planned.fetch_issues.front().transport_error.message;
        }
      } else if (!planned.catalog_issues.empty()) {
        message += ": " + planned.catalog_issues.front().message;
      } else if (!planned.resolution_issues.empty()) {
        message += ": " + planned.resolution_issues.front().message;
      }
      return message;
    }

    std::string artifact_fetch_failure(
        const modules::ArtifactFetchResult& fetched
    ) {
      std::string message{"selected module artifacts could not be fetched"};
      if (fetched.issues.empty()) return message;
      const auto& issue = fetched.issues.front();
      message += ": " + issue.message;
      if (issue.http_status.has_value()) {
        message += ": HTTP " + std::to_string(*issue.http_status);
      } else if (issue.transport_error.has_value()) {
        message += ": " + issue.transport_error->message;
      }
      return message;
    }

    std::string artifact_install_failure(
        const modules::ArtifactInstallResult& installed
    ) {
      std::string message{"selected module artifacts could not be installed"};
      if (installed.issues.empty()) return message;
      const auto& issue = installed.issues.front();
      message += ": " + issue.message;
      if (issue.system_error) message += ": " + issue.system_error.message();
      return message;
    }

  }  // namespace

  ProjectBootstrapResult bootstrap_project(
      const std::filesystem::path& manifest_path, ProjectBootstrapOptions options,
      http::Client* http_client
  ) {
    ProjectBootstrapResult result;
    result.profile = options.resolver.profile;
    auto source = detail::read_project_manifest_bounded(manifest_path);
    if (!source.ok()) {
      fail(
          result, ProjectBootstrapIssueCode::ReadManifest,
          std::move(source.error)
      );
      return result;
    }
    auto parsed = modules::parse_product_manifest(
        *source.contents, source.absolute_path.generic_string()
    );
    if (!parsed.ok()) {
      auto message = std::string{"mobagen.yaml is invalid"};
      if (!parsed.errors.empty()) message += ": " + parsed.errors.front().message;
      fail(result, ProjectBootstrapIssueCode::ParseManifest, std::move(message));
      return result;
    }
    result.product_name = parsed.descriptor->name;
    const auto manifest_hash = detail::hash_project_manifest(*source.contents);
    if (!manifest_hash.has_value()) {
      fail(
          result, ProjectBootstrapIssueCode::HashManifest,
          "mobagen.yaml could not be fingerprinted"
      );
      return result;
    }
    const auto profile = std::ranges::find(
        parsed.descriptor->profiles, options.resolver.profile,
        &modules::ProfileDescriptor::name
    );
    if (profile == parsed.descriptor->profiles.end()) {
      fail(
          result, ProjectBootstrapIssueCode::ProfileUnavailable,
          "selected profile is absent from mobagen.yaml"
      );
      return result;
    }

    std::string stale_reason;
    if (options.mode == ProjectBootstrapMode::Ensure) {
      const auto probe = probe_bootstrap(
          options, *parsed.descriptor, profile->linkage,
          source.absolute_path, *manifest_hash
      );
      if (probe.ready) {
        result.state = ProjectBootstrapState::Ready;
        result.plugin_count = probe.plugin_count;
        result.selected_count = probe.selected_count;
        result.lockfile_path = source.absolute_path.parent_path() / "mobagen.lock";
        return result;
      }
      stale_reason = probe.reason;
    }
    if (http_client == nullptr) {
      auto message = stale_reason.empty()
                         ? std::string{"HTTPS client is unavailable"}
                         : std::move(stale_reason)
                               + ": HTTPS client is unavailable";
      fail(
          result, ProjectBootstrapIssueCode::HttpUnavailable,
          std::move(message)
      );
      return result;
    }

    auto planned = modules::plan_module_sync(
        *parsed.descriptor, *http_client, options.resolver
    );
    if (!planned.ok()) {
      fail(
          result, ProjectBootstrapIssueCode::SyncPlan,
          sync_plan_failure(planned)
      );
      return result;
    }
    result.selections.reserve(planned.resolution->lifecycle_order().size());
    for (const auto provider_index : planned.resolution->lifecycle_order()) {
      const auto* provider = planned.catalog->registry().provider(provider_index);
      const auto* artifact = planned.catalog->artifact_for(provider_index);
      if (provider == nullptr || artifact == nullptr) {
        fail(
            result, ProjectBootstrapIssueCode::SyncPlan,
            "selected provider has no catalog artifact"
        );
        return result;
      }
      result.selections.push_back({
          .provider_id = provider->id,
          .version = provider->version,
          .artifact = *artifact,
      });
    }

    auto configured_cache = options.cache_root.value_or(
        std::filesystem::path{".mobagen"} / "cache"
    );
    if (configured_cache.is_relative()) {
      configured_cache = source.absolute_path.parent_path() / configured_cache;
    }
    std::error_code cache_path_error;
    auto cache_root = std::filesystem::absolute(
        configured_cache, cache_path_error
    ).lexically_normal();
    if (cache_path_error) {
      fail(
          result, ProjectBootstrapIssueCode::CachePath,
          "module cache path could not be resolved"
      );
      return result;
    }
    assets::AssetCache cache{
        cache_root,
        static_cast<std::size_t>(modules::max_module_artifact_bytes),
    };
    auto fetched = modules::fetch_module_artifacts(
        *planned.catalog, *planned.resolution, *http_client, cache
    );
    if (!fetched.ok()) {
      fail(
          result, ProjectBootstrapIssueCode::ArtifactFetch,
          artifact_fetch_failure(fetched)
      );
      return result;
    }

    const auto install_root = source.absolute_path.parent_path()
                              / ".mobagen" / "plugins";
    auto installed = modules::materialize_module_plugins(
        fetched.artifacts, install_root
    );
    if (!installed.ok()) {
      fail(
          result, ProjectBootstrapIssueCode::ArtifactInstall,
          artifact_install_failure(installed)
      );
      return result;
    }

    modules::LockfileMetadata lock_metadata{
        .sdk = options.sdk_version,
        .target = options.resolver.target,
        .profile = options.resolver.profile,
        .manifest_hash = *manifest_hash,
    };
    lock_metadata.plugins.reserve(installed.artifacts.size());
    for (const auto& artifact : installed.artifacts) {
      lock_metadata.plugins.push_back({
          .provider = artifact.provider_id,
          .version = artifact.version,
          .abi_version = artifact.abi_version,
          .package = artifact.package_path
                         .lexically_relative(source.absolute_path.parent_path())
                         .lexically_normal()
                         .generic_string(),
          .hash = assets::to_string(artifact.id),
      });
    }
    auto lockfile = modules::serialize_lockfile(
        planned.catalog->registry(), *planned.resolution, lock_metadata
    );
    if (!lockfile.ok()) {
      auto message = std::string{
          "selected remote modules could not be represented in mobagen.lock"
      };
      if (!lockfile.issues.empty()) {
        message += ": " + lockfile.issues.front().message;
      }
      fail(result, ProjectBootstrapIssueCode::Lockfile, std::move(message));
      return result;
    }
    result.lockfile_path = source.absolute_path.parent_path() / "mobagen.lock";
    const auto lock_written = modules::write_lockfile_atomic(
        result.lockfile_path, *lockfile.contents
    );
    if (!lock_written.ok()) {
      auto message = std::string{"mobagen.lock could not be updated"};
      if (lock_written.issue.has_value()) {
        message += ": " + lock_written.issue->message;
      }
      fail(result, ProjectBootstrapIssueCode::Lockfile, std::move(message));
      return result;
    }

    result.state = ProjectBootstrapState::Synchronized;
    result.plugin_count = installed.artifacts.size();
    result.selected_count = planned.resolution->lifecycle_order().size();
    result.cached_artifacts = std::move(fetched.artifacts);
    result.installed_artifacts = std::move(installed.artifacts);
    return result;
  }

}  // namespace mobagen::compositions
