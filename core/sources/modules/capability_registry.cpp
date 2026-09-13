#include "capability_registry.hpp"

#include <algorithm>
#include <iterator>
#include <tuple>
#include <utility>

namespace mobagen::modules {

  CapabilityRegistry::CapabilityRegistry(std::vector<ProviderDescriptor> providers, std::vector<std::string> capabilities,
                                         std::vector<std::vector<ProviderIndex>> providers_by_capability)
      : providers_(std::move(providers)), capabilities_(std::move(capabilities)), providers_by_capability_(std::move(providers_by_capability)) {}

  std::size_t CapabilityRegistry::provider_count() const noexcept { return providers_.size(); }

  std::size_t CapabilityRegistry::capability_count() const noexcept { return capabilities_.size(); }

  std::optional<ProviderIndex> CapabilityRegistry::find_provider(std::string_view provider_id) const noexcept {
    const auto found = std::ranges::lower_bound(providers_, provider_id, {}, &ProviderDescriptor::id);
    if (found == providers_.end() || found->id != provider_id) return std::nullopt;
    return ProviderIndex{static_cast<std::uint32_t>(std::distance(providers_.begin(), found))};
  }

  std::optional<CapabilityIndex> CapabilityRegistry::find_capability(std::string_view capability_id) const noexcept {
    const auto found = std::ranges::lower_bound(capabilities_, capability_id);
    if (found == capabilities_.end() || *found != capability_id) return std::nullopt;
    return CapabilityIndex{static_cast<std::uint32_t>(std::distance(capabilities_.begin(), found))};
  }

  const ProviderDescriptor* CapabilityRegistry::provider(ProviderIndex index) const noexcept {
    if (index.value >= providers_.size()) return nullptr;
    return &providers_[index.value];
  }

  std::string_view CapabilityRegistry::capability_name(CapabilityIndex index) const noexcept {
    if (index.value >= capabilities_.size()) return {};
    return capabilities_[index.value];
  }

  std::span<const ProviderIndex> CapabilityRegistry::providers_for(CapabilityIndex index) const noexcept {
    if (index.value >= providers_by_capability_.size()) return {};
    return providers_by_capability_[index.value];
  }

  void CapabilityRegistryBuilder::add(ProviderDescriptor provider) { providers_.push_back(std::move(provider)); }

  CapabilityRegistryBuildResult CapabilityRegistryBuilder::build() const {
    CapabilityRegistryBuildResult result;
    auto providers = providers_;
    std::ranges::sort(providers, [](const ProviderDescriptor& left, const ProviderDescriptor& right) {
      return std::tie(left.id, left.version.major, left.version.minor, left.version.patch)
             < std::tie(right.id, right.version.major, right.version.minor, right.version.patch);
    });

    for (const auto& provider : providers) {
      auto descriptor_issues = validate(provider);
      if (!descriptor_issues.empty()) {
        result.issues.push_back({RegistryIssueCode::InvalidDescriptor, provider.id, "provider descriptor is invalid", std::move(descriptor_issues)});
      }
    }
    for (std::size_t index = 1; index < providers.size(); ++index) {
      if (providers[index - 1].id == providers[index].id) {
        result.issues.push_back({RegistryIssueCode::DuplicateProvider, providers[index].id, "provider IDs must be unique", {}});
      }
    }
    if (!result.issues.empty()) return result;

    std::vector<std::string> capabilities;
    for (const auto& provider : providers) {
      capabilities.insert(capabilities.end(), provider.provides.begin(), provider.provides.end());
    }
    std::ranges::sort(capabilities);
    capabilities.erase(std::ranges::unique(capabilities).begin(), capabilities.end());

    std::vector<std::vector<ProviderIndex>> providers_by_capability(capabilities.size());
    for (std::size_t provider_offset = 0; provider_offset < providers.size(); ++provider_offset) {
      for (const auto& capability : providers[provider_offset].provides) {
        const auto found = std::ranges::lower_bound(capabilities, capability);
        const auto capability_offset = static_cast<std::size_t>(std::distance(capabilities.begin(), found));
        providers_by_capability[capability_offset].push_back(ProviderIndex{static_cast<std::uint32_t>(provider_offset)});
      }
    }

    result.registry = CapabilityRegistry{std::move(providers), std::move(capabilities), std::move(providers_by_capability)};
    return result;
  }

}  // namespace mobagen::modules
