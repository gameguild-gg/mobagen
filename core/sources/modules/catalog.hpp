#pragma once

#include "descriptor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mobagen::modules {

  inline constexpr std::uint32_t module_catalog_schema_version = 1;
  inline constexpr std::uint64_t max_module_artifact_bytes = 512ULL * 1024ULL * 1024ULL;
  inline constexpr std::size_t max_module_catalog_providers = 1024;
  inline constexpr std::size_t max_module_provider_artifacts = 1024;

  struct ModuleArtifactDescriptor {
    TargetPlatform target{};
    LinkageMode linkage{};
    std::uint32_t abi_version{};
    std::string url;
    std::uint64_t size{};
    std::string hash;
  };

  struct PublishedProviderDescriptor {
    ProviderDescriptor provider;
    std::vector<ModuleArtifactDescriptor> artifacts;
  };

  struct ModuleCatalogDescriptor {
    std::uint32_t schema{module_catalog_schema_version};
    std::vector<PublishedProviderDescriptor> providers;
  };

}  // namespace mobagen::modules
