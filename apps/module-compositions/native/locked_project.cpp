#include "locked_project.hpp"

#include "project_support.hpp"

#include "modules/locked_activation_plan.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <span>
#include <string_view>
#include <utility>

namespace mobagen::compositions {
  namespace {

    void add_issue(LockedNativeProjectResult& result, LockedNativeProjectIssue issue) {
      result.issues.push_back(std::move(issue));
    }

    std::optional<std::string> selected_provider(
        const modules::ModuleRequest& request, const modules::LockfileDocument& lock
    ) {
      if (modules::is_provider_id(request.provider)) return request.provider;
      if (request.capability.empty()) return std::nullopt;
      const auto selection = std::ranges::find(
          lock.resolved, request.capability, &modules::LockedProviderSelection::capability
      );
      return selection == lock.resolved.end()
               ? std::nullopt
               : std::optional<std::string>{selection->provider};
    }

    bool validate_project_lock_contract(
        const modules::ProductDescriptor& product, const modules::LockfileDocument& lock,
        std::string_view profile_name, LockedNativeProjectResult& result
    ) {
      const auto profile = std::ranges::find(
          product.profiles, profile_name, &modules::ProfileDescriptor::name
      );
      if (profile == product.profiles.end()) {
        add_issue(result, {
            .code = LockedNativeProjectIssueCode::ProjectMismatch,
            .message = "mobagen.lock selects a profile absent from mobagen.yaml",
        });
        return false;
      }
      if (profile->linkage != modules::LinkageMode::Dynamic) {
        add_issue(result, {
            .code = LockedNativeProjectIssueCode::ProjectMismatch,
            .message = "locked native startup requires a dynamic manifest profile",
        });
        return false;
      }
      for (const auto& permission : lock.permissions) {
        if (std::ranges::find(profile->permissions, permission) == profile->permissions.end()) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::ProjectMismatch,
              .message = "mobagen.lock grants a permission absent from its manifest profile",
          });
          return false;
        }
      }
      for (const auto& selection : lock.resolved) {
        if (selection.linkage != profile->linkage) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::ProjectMismatch,
              .message = "mobagen.lock linkage does not match its manifest profile",
          });
          return false;
        }
      }

      std::vector<std::string_view> reachable_capabilities;
      reachable_capabilities.reserve(product.modules.size() + lock.dependencies.size());
      for (const auto& request : product.modules) {
        if (request.capability.empty()) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::ProjectMismatch,
              .message = "locked startup requires every manifest module to declare its capability",
          });
          return false;
        }
        const auto selection = std::ranges::find(
            lock.resolved, request.capability, &modules::LockedProviderSelection::capability
        );
        if (selection == lock.resolved.end()
            || (modules::is_provider_id(request.provider)
                && request.provider != selection->provider)) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::ProjectMismatch,
              .message = "mobagen.lock does not satisfy a manifest module selection",
          });
          return false;
        }
        reachable_capabilities.push_back(request.capability);
      }
      for (const auto& dependency : lock.dependencies) {
        reachable_capabilities.push_back(dependency.capability);
      }
      for (const auto& selection : lock.resolved) {
        if (std::ranges::find(reachable_capabilities, selection.capability)
            == reachable_capabilities.end()) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::ProjectMismatch,
              .message = "mobagen.lock selects a capability unreachable from mobagen.yaml",
          });
          return false;
        }
      }
      return true;
    }

    std::optional<std::vector<NativeModuleConfiguration>> collect_configurations(
        const modules::ProductDescriptor& product, const modules::LockfileDocument& lock,
        const modules::LockedPluginActivationPlan& plan, LockedNativeProjectResult& result
    ) {
      std::map<std::string, std::vector<std::byte>, std::less<>> configurations;
      for (const auto& request : product.modules) {
        if (!request.configuration.has_value()) continue;
        const auto provider = selected_provider(request, lock);
        if (!provider.has_value()) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::Configuration,
              .message = "configured module cannot be mapped to its locked provider",
          });
          return std::nullopt;
        }
        if (plan.find(*provider) == nullptr) continue;
        const auto bytes = std::as_bytes(std::span{
            request.configuration->data.data(), request.configuration->data.size()
        });
        std::vector<std::byte> owned{bytes.begin(), bytes.end()};
        const auto [existing, inserted] = configurations.emplace(*provider, owned);
        if (!inserted && existing->second != owned) {
          add_issue(result, {
              .code = LockedNativeProjectIssueCode::Configuration,
              .message = "one locked plugin receives conflicting module configurations",
          });
          return std::nullopt;
        }
      }

      std::vector<NativeModuleConfiguration> collected;
      collected.reserve(configurations.size());
      for (auto& [provider, data] : configurations) {
        collected.push_back({std::move(provider), std::move(data)});
      }
      return collected;
    }

  }  // namespace

  LockedNativeProjectResult open_locked_native_project(
      const std::filesystem::path& manifest_path, LockedNativeProjectOptions options,
      plugins::PluginLogSink log_sink, void* log_context
  ) {
    LockedNativeProjectResult result;
    auto source = detail::read_project_manifest_bounded(manifest_path);
    if (!source.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ReadManifest,
          .message = std::move(source.error),
      });
      return result;
    }
    auto parsed = modules::parse_product_manifest(
        *source.contents, source.absolute_path.generic_string()
    );
    if (!parsed.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ParseManifest,
          .message = "mobagen.yaml is invalid",
          .manifest_errors = std::move(parsed.errors),
      });
      return result;
    }
    const auto manifest_hash = detail::hash_project_manifest(*source.contents);
    if (!manifest_hash.has_value()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::HashManifest,
          .message = "mobagen.yaml could not be fingerprinted",
      });
      return result;
    }

    const auto project_root = source.absolute_path.parent_path();
    auto lock_source = modules::read_lockfile_bounded(project_root / "mobagen.lock");
    if (!lock_source.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ReadLock,
          .message = "bootstrapped native project requires a readable mobagen.lock",
          .lockfile_read_issue = std::move(lock_source.issue),
      });
      return result;
    }
    auto lock = modules::parse_lockfile(*lock_source.contents);
    if (!lock.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ParseLock,
          .message = "mobagen.lock is invalid",
          .lockfile_parse_issues = std::move(lock.issues),
      });
      return result;
    }
    if (!validate_project_lock_contract(
            *parsed.descriptor, *lock.document, options.profile, result
        )) {
      return result;
    }
    auto inspected = modules::inspect_locked_project(
        *lock.document, project_root,
        {
            .sdk = options.sdk_version,
            .target = options.target,
            .profile = options.profile,
            .manifest_hash = *manifest_hash,
        }
    );
    if (!inspected.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::VerifyLock,
          .message = "mobagen.lock or its installed plugin packages failed inspection",
          .verification_issues = std::move(inspected.issues),
      });
      return result;
    }
    auto planned = modules::build_locked_plugin_activation_plan(
        *lock.document, inspected.plugins
    );
    if (!planned.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ActivationPlan,
          .message = "mobagen.lock cannot form a deterministic activation plan",
          .activation_plan_issues = std::move(planned.issues),
      });
      return result;
    }
    auto configurations = collect_configurations(
        *parsed.descriptor, *lock.document, *planned.plan, result
    );
    if (!configurations.has_value()) return result;
    auto manager = create_native_module_manager(
        std::move(planned.plan), *configurations, log_sink, log_context
    );
    if (!manager.ok()) {
      add_issue(result, {
          .code = LockedNativeProjectIssueCode::ModuleManager,
          .message = "locked native module manager could not be created",
          .manager_issues = std::move(manager.issues),
      });
      return result;
    }

    result.manager = std::move(manager.manager);
    result.product = std::move(parsed.descriptor);
    return result;
  }

}  // namespace mobagen::compositions
