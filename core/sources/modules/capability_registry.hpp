#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "descriptor.hpp"

namespace mobagen::modules {

  struct RegistryGeneration {
    std::uint64_t value{};

    friend bool operator==(const RegistryGeneration&, const RegistryGeneration&) = default;
  };

  struct ProviderIndex {
    std::uint32_t value{};

    friend bool operator==(const ProviderIndex&, const ProviderIndex&) = default;
  };

  struct CapabilityIndex {
    std::uint32_t value{};

    friend bool operator==(const CapabilityIndex&, const CapabilityIndex&) = default;
  };

  enum class RegistryIssueCode : std::uint8_t { InvalidDescriptor, DuplicateProvider };

  struct RegistryIssue {
    RegistryIssueCode code{};
    std::string provider_id;
    std::string message;
    std::vector<DescriptorIssue> descriptor_issues;
  };

  class CapabilityRegistryBuilder;

  class CapabilityRegistry {
  public:
    [[nodiscard]] RegistryGeneration generation() const noexcept { return generation_; }
    [[nodiscard]] std::size_t provider_count() const noexcept;
    [[nodiscard]] std::size_t capability_count() const noexcept;

    [[nodiscard]] std::optional<ProviderIndex> find_provider(std::string_view provider_id) const noexcept;
    [[nodiscard]] std::optional<CapabilityIndex> find_capability(std::string_view capability_id) const noexcept;

    [[nodiscard]] const ProviderDescriptor* provider(ProviderIndex index) const noexcept;
    [[nodiscard]] std::string_view capability_name(CapabilityIndex index) const noexcept;
    [[nodiscard]] std::span<const ProviderIndex> providers_for(CapabilityIndex index) const noexcept;

  private:
    friend class CapabilityRegistryBuilder;

    CapabilityRegistry(RegistryGeneration generation, std::vector<ProviderDescriptor> providers, std::vector<std::string> capabilities,
                       std::vector<std::vector<ProviderIndex>> providers_by_capability);

    RegistryGeneration generation_;
    std::vector<ProviderDescriptor> providers_;
    std::vector<std::string> capabilities_;
    std::vector<std::vector<ProviderIndex>> providers_by_capability_;
  };

  struct CapabilityRegistryBuildResult {
    std::optional<CapabilityRegistry> registry;
    std::vector<RegistryIssue> issues;

    [[nodiscard]] bool ok() const noexcept { return registry.has_value(); }
  };

  class CapabilityRegistryBuilder {
  public:
    void add(ProviderDescriptor provider);
    [[nodiscard]] CapabilityRegistryBuildResult build() const;

  private:
    std::vector<ProviderDescriptor> providers_;
  };

}  // namespace mobagen::modules
