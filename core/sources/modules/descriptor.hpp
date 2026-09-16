#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::modules {

  inline constexpr std::uint32_t project_schema_version = 1;
  inline constexpr std::size_t max_module_configuration_bytes = 1024 * 1024;
  inline constexpr std::size_t max_module_source_url_bytes = 2048;

  struct SemanticVersion {
    std::uint32_t major{};
    std::uint32_t minor{};
    std::uint32_t patch{};

    friend bool operator==(const SemanticVersion&, const SemanticVersion&) = default;
  };

  enum class LinkageMode : std::uint8_t { Static, Dynamic, Wasm, Process };

  enum class TargetPlatform : std::uint8_t { Windows, Linux, MacOS, Web, Android, IOS };

  enum class ReloadPolicy : std::uint8_t { Never, Restart, SafePoint };

  struct ModuleConfiguration {
    std::string schema;
    std::string data;

    friend bool operator==(const ModuleConfiguration&, const ModuleConfiguration&) = default;
  };

  struct ModuleRequest {
    std::string alias;
    std::string provider;
    std::optional<ModuleConfiguration> configuration;
    std::string capability;
  };

  struct ProfileDescriptor {
    std::string name;
    LinkageMode linkage{LinkageMode::Static};
    bool editor{true};
    std::vector<std::string> permissions;
  };

  struct ModuleSourceDescriptor {
    std::string name;
    std::string url;
  };

  struct ProductDescriptor {
    std::uint32_t schema{project_schema_version};
    std::string name;
    std::vector<ModuleSourceDescriptor> sources;
    std::vector<ModuleRequest> modules;
    std::vector<std::string> plugins;
    std::vector<ProfileDescriptor> profiles;
  };

  struct ProviderDescriptor {
    std::string id;
    SemanticVersion version;
    std::vector<std::string> provides;
    std::vector<std::string> required;
    std::vector<std::string> optional;
    std::vector<std::string> conflicts;
    std::vector<TargetPlatform> targets;
    std::vector<LinkageMode> linkages;
    ReloadPolicy reload{ReloadPolicy::Never};
    std::string configuration_schema;
    std::vector<std::string> permissions;
  };

  enum class DescriptorIssueCode : std::uint8_t {
    UnsupportedSchema,
    InvalidIdentifier,
    InvalidCapability,
    InvalidPluginPath,
    InvalidSourceUrl,
    LimitExceeded,
    DuplicateEntry,
    MissingEntry,
    SelfDependency,
  };

  struct DescriptorIssue {
    DescriptorIssueCode code{};
    std::string field;
    std::string message;
  };

  [[nodiscard]] bool is_slug(std::string_view value) noexcept;
  [[nodiscard]] bool is_provider_id(std::string_view value) noexcept;
  [[nodiscard]] bool is_capability_id(std::string_view value) noexcept;
  [[nodiscard]] bool is_secure_https_url(std::string_view value) noexcept;
  [[nodiscard]] bool is_secure_plugin_artifact_url(std::string_view value) noexcept;
  [[nodiscard]] std::vector<DescriptorIssue> validate(const ProductDescriptor& descriptor);
  [[nodiscard]] std::vector<DescriptorIssue> validate(const ProviderDescriptor& descriptor);

}  // namespace mobagen::modules
