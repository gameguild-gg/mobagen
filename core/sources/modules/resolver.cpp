#include "resolver.hpp"

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace mobagen::modules {

  namespace {

    void add_issue(ResolutionResult& result, ResolutionIssueCode code, std::string module_alias, std::string capability, std::string provider_id,
                   std::string message, std::vector<DescriptorIssue> descriptor_issues = {}) {
      result.issues.push_back(
          {code, std::move(module_alias), std::move(capability), std::move(provider_id), std::move(message), std::move(descriptor_issues)});
    }

    const ProfileDescriptor* find_profile(const ProductDescriptor& product, std::string_view profile_name) {
      const auto found = std::ranges::find(product.profiles, profile_name, &ProfileDescriptor::name);
      return found == product.profiles.end() ? nullptr : &*found;
    }

    const ModuleAliasBinding* find_alias(const std::vector<ModuleAliasBinding>& aliases, std::string_view alias) {
      const auto found = std::ranges::lower_bound(aliases, alias, {}, &ModuleAliasBinding::alias);
      return found == aliases.end() || found->alias != alias ? nullptr : &*found;
    }

    const DefaultProviderBinding* find_default(const std::vector<DefaultProviderBinding>& defaults, TargetPlatform target, std::string_view profile,
                                               std::string_view capability) {
      const auto found = std::ranges::find_if(defaults, [&](const DefaultProviderBinding& candidate) {
        return candidate.target == target && candidate.profile == profile && candidate.capability == capability;
      });
      return found == defaults.end() ? nullptr : &*found;
    }

    bool supports(const ProviderDescriptor& provider, TargetPlatform target) {
      return std::ranges::find(provider.targets, target) != provider.targets.end();
    }

    bool supports(const ProviderDescriptor& provider, LinkageMode linkage) {
      return std::ranges::find(provider.linkages, linkage) != provider.linkages.end();
    }

    bool provides(const ProviderDescriptor& provider, std::string_view capability) {
      return std::ranges::find(provider.provides, capability) != provider.provides.end();
    }

    struct StagedResolution {
      const CapabilityRegistry& registry;
      const ResolverOptions& options;
      const ProfileDescriptor& profile;
      const std::vector<DefaultProviderBinding>& defaults;
      ResolutionResult& result;
      std::map<std::uint32_t, ResolvedCapability> selections;
      std::set<std::uint32_t> selected_providers;
      std::set<std::uint32_t> pending_providers;
      std::set<std::uint32_t> processed_providers;
      std::vector<ProviderDependency> dependencies;

      bool compatible(ProviderIndex provider_index, std::string_view module_alias, std::string_view capability) {
        const auto* provider = registry.provider(provider_index);
        if (provider == nullptr) {
          add_issue(result, ResolutionIssueCode::UnknownProvider, std::string(module_alias), std::string(capability), {},
                    "selected provider index is not registered");
          return false;
        }
        if (!supports(*provider, options.target)) {
          add_issue(result, ResolutionIssueCode::UnsupportedTarget, std::string(module_alias), std::string(capability), provider->id,
                    "selected provider does not support the active target");
          return false;
        }
        if (!supports(*provider, profile.linkage)) {
          add_issue(result, ResolutionIssueCode::UnsupportedLinkage, std::string(module_alias), std::string(capability), provider->id,
                    "selected provider does not support the profile linkage");
          return false;
        }
        return true;
      }

      bool select(ProviderIndex provider_index, CapabilityIndex requested_capability, std::string reason, std::string_view module_alias) {
        const auto* provider = registry.provider(provider_index);
        const auto capability = registry.capability_name(requested_capability);
        if (provider == nullptr) {
          add_issue(result, ResolutionIssueCode::UnknownProvider, std::string(module_alias), std::string(capability), {},
                    "selected provider index is not registered");
          return false;
        }
        if (!provides(*provider, capability)) {
          add_issue(result, ResolutionIssueCode::ProviderDoesNotProvide, std::string(module_alias), std::string(capability), provider->id,
                    "selected provider does not expose the requested capability");
          return false;
        }
        if (!compatible(provider_index, module_alias, capability)) return false;

        std::vector<std::string> provided_capabilities = provider->provides;
        std::ranges::sort(provided_capabilities);
        for (const auto& provided_capability : provided_capabilities) {
          const auto capability_index = registry.find_capability(provided_capability);
          if (!capability_index.has_value()) continue;

          const auto existing = selections.find(capability_index->value);
          if (existing != selections.end() && existing->second.provider != provider_index) {
            add_issue(result, ResolutionIssueCode::ConflictingSelection, std::string(module_alias), provided_capability, provider->id,
                      "capability was already assigned to a different provider");
            continue;
          }

          std::string capability_reason = reason;
          if (*capability_index != requested_capability) capability_reason = "provided by selected provider '" + provider->id + "'";
          selections.insert_or_assign(capability_index->value,
                                      ResolvedCapability{*capability_index, provider_index, profile.linkage, std::move(capability_reason)});
        }

        if (selected_providers.insert(provider_index.value).second) pending_providers.insert(provider_index.value);
        return true;
      }

      std::optional<ProviderIndex> named_provider(std::string_view provider_id, CapabilityIndex capability, std::string_view module_alias) {
        const auto capability_name = registry.capability_name(capability);
        const auto provider_index = registry.find_provider(provider_id);
        if (!provider_index.has_value()) {
          add_issue(result, ResolutionIssueCode::UnknownProvider, std::string(module_alias), std::string(capability_name), std::string(provider_id),
                    "selected provider is not registered");
          return std::nullopt;
        }
        const auto* provider = registry.provider(*provider_index);
        if (provider == nullptr || !provides(*provider, capability_name)) {
          add_issue(result, ResolutionIssueCode::ProviderDoesNotProvide, std::string(module_alias), std::string(capability_name),
                    std::string(provider_id), "selected provider does not expose the requested capability");
          return std::nullopt;
        }
        if (!compatible(*provider_index, module_alias, capability_name)) return std::nullopt;
        return provider_index;
      }

      std::optional<ProviderIndex> dependency_provider(CapabilityIndex capability, const ProviderDescriptor& dependent) {
        const auto capability_name = registry.capability_name(capability);
        std::vector<ProviderIndex> compatible_providers;
        for (const auto candidate : registry.providers_for(capability)) {
          const auto* descriptor = registry.provider(candidate);
          if (descriptor != nullptr && supports(*descriptor, options.target) && supports(*descriptor, profile.linkage)) {
            compatible_providers.push_back(candidate);
          }
        }

        if (compatible_providers.empty()) {
          add_issue(result, ResolutionIssueCode::MissingCapability, {}, std::string(capability_name), dependent.id,
                    "no compatible provider exposes a required capability");
          return std::nullopt;
        }
        if (compatible_providers.size() == 1) return compatible_providers.front();

        const auto* default_provider = find_default(defaults, options.target, options.profile, capability_name);
        if (default_provider == nullptr) {
          add_issue(result, ResolutionIssueCode::AmbiguousProvider, {}, std::string(capability_name), dependent.id,
                    "multiple compatible providers expose a required capability and no default resolves the tie");
          return std::nullopt;
        }
        return named_provider(default_provider->provider, capability, {});
      }

      void expand_dependencies() {
        while (!pending_providers.empty()) {
          const auto provider_value = *pending_providers.begin();
          pending_providers.erase(pending_providers.begin());
          if (!processed_providers.insert(provider_value).second) continue;

          const ProviderIndex dependent_index{provider_value};
          const auto* dependent = registry.provider(dependent_index);
          if (dependent == nullptr) continue;
          auto required_capabilities = dependent->required;
          std::ranges::sort(required_capabilities);
          for (const auto& required_capability : required_capabilities) {
            const auto capability = registry.find_capability(required_capability);
            if (!capability.has_value()) {
              add_issue(result, ResolutionIssueCode::MissingCapability, {}, required_capability, dependent->id,
                        "no provider exposes a required capability");
              continue;
            }

            auto selected = selections.find(capability->value);
            if (selected == selections.end()) {
              const auto provider = dependency_provider(*capability, *dependent);
              if (!provider.has_value()) continue;
              select(*provider, *capability, "required by provider '" + dependent->id + "'", {});
              selected = selections.find(capability->value);
              if (selected == selections.end()) continue;
            }
            dependencies.push_back({selected->second.provider, dependent_index, *capability});
          }
        }
      }

      void validate_conflicts() {
        std::set<std::pair<std::uint32_t, std::uint32_t>> reported;
        for (const auto provider_value : selected_providers) {
          const ProviderIndex provider_index{provider_value};
          const auto* provider = registry.provider(provider_index);
          if (provider == nullptr) continue;
          auto conflicts = provider->conflicts;
          std::ranges::sort(conflicts);
          for (const auto& conflict_id : conflicts) {
            const auto conflict = registry.find_provider(conflict_id);
            if (!conflict.has_value() || !selected_providers.contains(conflict->value)) continue;
            const auto pair = std::minmax(provider_value, conflict->value);
            if (!reported.emplace(pair.first, pair.second).second) continue;
            add_issue(result, ResolutionIssueCode::ProviderConflict, {}, {}, provider->id,
                      "selected provider conflicts with selected provider '" + conflict_id + "'");
          }
        }
      }

      std::vector<ProviderIndex> topological_order() {
        std::map<std::uint32_t, std::set<std::uint32_t>> dependents;
        std::map<std::uint32_t, std::size_t> indegree;
        for (const auto provider : selected_providers) indegree.emplace(provider, 0);
        for (const auto& edge : dependencies) {
          if (dependents[edge.dependency.value].insert(edge.dependent.value).second) ++indegree[edge.dependent.value];
        }

        std::set<std::uint32_t> ready;
        for (const auto& [provider, count] : indegree) {
          if (count == 0) ready.insert(provider);
        }

        std::vector<ProviderIndex> order;
        while (!ready.empty()) {
          const auto provider = *ready.begin();
          ready.erase(ready.begin());
          order.push_back(ProviderIndex{provider});
          for (const auto dependent : dependents[provider]) {
            auto& count = indegree[dependent];
            --count;
            if (count == 0) ready.insert(dependent);
          }
        }
        if (order.size() != selected_providers.size()) {
          add_issue(result, ResolutionIssueCode::DependencyCycle, {}, {}, {}, "selected providers contain a dependency cycle");
          return {};
        }
        return order;
      }
    };

  }  // namespace

  struct ModuleResolutionBuilder {
    ModuleResolution resolution;

    void add_selection(ResolvedCapability selection) { resolution.selections_.push_back(std::move(selection)); }
    void add_dependency(ProviderDependency dependency) { resolution.dependencies_.push_back(dependency); }
    void add_provider(ProviderIndex provider) { resolution.lifecycle_order_.push_back(provider); }
  };

  const ResolvedCapability* ModuleResolution::selection_for(CapabilityIndex capability) const noexcept {
    const auto found
        = std::ranges::lower_bound(selections_, capability.value, {}, [](const ResolvedCapability& selection) { return selection.capability.value; });
    if (found == selections_.end() || found->capability != capability) return nullptr;
    return &*found;
  }

  std::span<const ResolvedCapability> ModuleResolution::selections() const noexcept { return selections_; }

  std::span<const ProviderDependency> ModuleResolution::dependencies() const noexcept { return dependencies_; }

  std::span<const ProviderIndex> ModuleResolution::lifecycle_order() const noexcept { return lifecycle_order_; }

  ResolutionResult resolve_modules(const ProductDescriptor& product, const CapabilityRegistry& registry, const ResolverOptions& options) {
    ResolutionResult result;
    const auto product_issues = validate(product);
    if (!product_issues.empty()) {
      add_issue(result, ResolutionIssueCode::InvalidProduct, {}, {}, {}, "product descriptor is invalid", product_issues);
      return result;
    }

    const auto* profile = find_profile(product, options.profile);
    if (profile == nullptr) {
      add_issue(result, ResolutionIssueCode::UnknownProfile, {}, {}, {}, "active profile is not declared by the product");
      return result;
    }

    auto aliases = options.aliases;
    std::ranges::sort(aliases, [](const ModuleAliasBinding& left, const ModuleAliasBinding& right) {
      return std::tie(left.alias, left.capability) < std::tie(right.alias, right.capability);
    });
    for (std::size_t index = 1; index < aliases.size(); ++index) {
      if (aliases[index - 1].alias == aliases[index].alias) {
        add_issue(result, ResolutionIssueCode::DuplicateAlias, aliases[index].alias, aliases[index].capability, {},
                  "module aliases must have exactly one capability binding");
      }
    }

    auto defaults = options.defaults;
    std::ranges::sort(defaults, [](const DefaultProviderBinding& left, const DefaultProviderBinding& right) {
      return std::tie(left.target, left.profile, left.capability, left.provider)
             < std::tie(right.target, right.profile, right.capability, right.provider);
    });
    for (std::size_t index = 1; index < defaults.size(); ++index) {
      const auto& previous = defaults[index - 1];
      const auto& current = defaults[index];
      if (previous.target == current.target && previous.profile == current.profile && previous.capability == current.capability) {
        add_issue(result, ResolutionIssueCode::DuplicateDefault, {}, current.capability, current.provider,
                  "target, profile, and capability must identify exactly one default provider");
      }
    }
    if (!result.issues.empty()) return result;

    StagedResolution staged{registry, options, *profile, defaults, result};
    auto requests = product.modules;
    std::ranges::sort(requests, {}, &ModuleRequest::alias);
    for (const auto& request : requests) {
      const auto* alias = find_alias(aliases, request.alias);
      if (alias == nullptr) {
        add_issue(result, ResolutionIssueCode::UnknownAlias, request.alias, {}, {}, "module alias has no capability binding");
        continue;
      }

      const auto capability = registry.find_capability(alias->capability);
      if (!capability.has_value()) {
        add_issue(result, ResolutionIssueCode::MissingCapability, request.alias, alias->capability, {},
                  "no provider exposes the requested capability");
        continue;
      }

      std::string provider_id = request.provider;
      std::string reason = "explicit provider for module '" + request.alias + "'";
      if (request.provider == "default") {
        const auto* default_provider = find_default(defaults, options.target, options.profile, alias->capability);
        if (default_provider == nullptr) {
          add_issue(result, ResolutionIssueCode::MissingDefault, request.alias, alias->capability, {},
                    "no default provider is declared for the active target and profile");
          continue;
        }
        provider_id = default_provider->provider;
        reason = "default for profile '" + options.profile + "'";
      }

      const auto provider = staged.named_provider(provider_id, *capability, request.alias);
      if (provider.has_value()) staged.select(*provider, *capability, std::move(reason), request.alias);
    }
    if (!result.issues.empty()) return result;

    staged.expand_dependencies();
    if (!result.issues.empty()) return result;
    staged.validate_conflicts();
    if (!result.issues.empty()) return result;
    auto lifecycle_order = staged.topological_order();
    if (!result.issues.empty()) return result;

    std::ranges::sort(staged.dependencies, [](const ProviderDependency& left, const ProviderDependency& right) {
      return std::tie(left.dependency.value, left.dependent.value, left.capability.value)
             < std::tie(right.dependency.value, right.dependent.value, right.capability.value);
    });
    staged.dependencies.erase(std::ranges::unique(staged.dependencies).begin(), staged.dependencies.end());

    ModuleResolutionBuilder builder;
    for (auto& [_, selection] : staged.selections) builder.add_selection(std::move(selection));
    for (const auto dependency : staged.dependencies) builder.add_dependency(dependency);
    for (const auto provider : lifecycle_order) builder.add_provider(provider);
    result.resolution = std::move(builder.resolution);
    return result;
  }

}  // namespace mobagen::modules
