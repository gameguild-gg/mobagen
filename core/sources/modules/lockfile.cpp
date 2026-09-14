#include "lockfile.hpp"

#include <algorithm>
#include <cstddef>
#include <locale>
#include <ostream>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

namespace mobagen::modules {

  namespace {

    struct SerializedProvider {
      std::string capability;
      std::string provider;
      SemanticVersion version;
      LinkageMode linkage{};
    };

    struct SerializedDependency {
      std::string capability;
      std::string provider;
      std::string required_by;
    };

    void add_issue(LockfileSerializeResult& result, LockfileIssueCode code, std::string field, std::string message) {
      result.issues.push_back({code, std::move(field), std::move(message)});
    }

    bool is_sha256(std::string_view value) {
      constexpr std::string_view prefix = "sha256:";
      if (!value.starts_with(prefix) || value.size() != prefix.size() + 64) return false;
      return std::ranges::all_of(value.substr(prefix.size()),
                                 [](char digit) { return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'); });
    }

    bool is_portable_package_path(std::string_view value) {
      if (value.empty() || value.contains('\\')) return false;

      const std::filesystem::path path{value};
      if (path.is_absolute() || path.has_root_path() || path.extension() != ".plugin" || path.lexically_normal().generic_string() != value) {
        return false;
      }
      for (const auto& component : path) {
        const auto text = component.generic_string();
        if (text.empty() || text == "." || text == ".."
            || !std::ranges::all_of(text, [](unsigned char value_char) { return value_char >= 0x20U && value_char != 0x7fU; })) {
          return false;
        }
      }
      return true;
    }

    void write_yaml_string(std::ostream& output, std::string_view value) {
      output << '"';
      for (const char value_char : value) {
        if (value_char == '"' || value_char == '\\') {
          output << '\\';
        }
        output << value_char;
      }
      output << '"';
    }

    std::string_view target_name(TargetPlatform target) {
      switch (target) {
        case TargetPlatform::Windows:
          return "windows";
        case TargetPlatform::Linux:
          return "linux";
        case TargetPlatform::MacOS:
          return "macos";
        case TargetPlatform::Web:
          return "web";
        case TargetPlatform::Android:
          return "android";
        case TargetPlatform::IOS:
          return "ios";
      }
      return {};
    }

    std::string_view linkage_name(LinkageMode linkage) {
      switch (linkage) {
        case LinkageMode::Static:
          return "static";
        case LinkageMode::Dynamic:
          return "dynamic";
        case LinkageMode::Wasm:
          return "wasm";
        case LinkageMode::Process:
          return "process";
      }
      return {};
    }

    void write_version(std::ostream& output, SemanticVersion version) { output << version.major << '.' << version.minor << '.' << version.patch; }

  }  // namespace

  LockfileSerializeResult serialize_lockfile(const CapabilityRegistry& registry, const ModuleResolution& resolution,
                                             const LockfileMetadata& metadata) {
    LockfileSerializeResult result;
    if (metadata.schema != lockfile_schema_version) {
      add_issue(result, LockfileIssueCode::UnsupportedSchema, "schema", "only lockfile schema version 1 is supported");
    }
    if (!is_slug(metadata.profile)) {
      add_issue(result, LockfileIssueCode::InvalidValue, "profile", "expected a lowercase profile slug");
    }
    if (target_name(metadata.target).empty()) {
      add_issue(result, LockfileIssueCode::InvalidValue, "target", "target is not supported by the lockfile schema");
    }

    auto plugins = metadata.plugins;
    std::ranges::sort(plugins, [](const PluginLockEntry& left, const PluginLockEntry& right) {
      return std::tie(left.provider, left.version.major, left.version.minor, left.version.patch, left.abi_version, left.package, left.hash)
             < std::tie(right.provider, right.version.major, right.version.minor, right.version.patch, right.abi_version, right.package, right.hash);
    });
    for (std::size_t index = 0; index < plugins.size(); ++index) {
      const auto& plugin = plugins[index];
      const std::string field = "plugins." + plugin.provider;
      if (!is_provider_id(plugin.provider)) {
        add_issue(result, LockfileIssueCode::InvalidValue, field, "expected a lowercase dotted provider ID");
      }
      if (plugin.abi_version == 0) {
        add_issue(result, LockfileIssueCode::InvalidValue, field + ".abi", "plugin ABI version must be positive");
      }
      if (!is_portable_package_path(plugin.package)) {
        add_issue(result, LockfileIssueCode::InvalidValue, field + ".package", "plugin package must be a portable relative path ending in .plugin");
      }
      if (!is_sha256(plugin.hash)) {
        add_issue(result, LockfileIssueCode::InvalidHash, field + ".hash", "expected sha256 followed by 64 lowercase hexadecimal digits");
      }
      if (index > 0 && plugins[index - 1].provider == plugin.provider) {
        add_issue(result, LockfileIssueCode::DuplicateEntry, field, "plugin provider IDs must be unique");
      }
    }

    std::vector<SerializedProvider> providers;
    std::set<std::string> capabilities;
    for (const auto& selection : resolution.selections()) {
      const auto capability = registry.capability_name(selection.capability);
      const auto* provider = registry.provider(selection.provider);
      if (capability.empty() || provider == nullptr || linkage_name(selection.linkage).empty()) {
        add_issue(result, LockfileIssueCode::InvalidResolution, "resolved", "resolution contains an invalid capability, provider, or linkage index");
        continue;
      }
      if (!capabilities.insert(std::string(capability)).second) {
        add_issue(result, LockfileIssueCode::InvalidResolution, "resolved." + std::string(capability),
                  "resolution contains duplicate capability selections");
        continue;
      }
      providers.push_back({std::string(capability), provider->id, provider->version, selection.linkage});
    }
    std::ranges::sort(providers, [](const SerializedProvider& left, const SerializedProvider& right) {
      return std::tie(left.capability, left.provider) < std::tie(right.capability, right.provider);
    });

    std::set<std::string> permissions;
    for (const auto provider_index : resolution.lifecycle_order()) {
      const auto* provider = registry.provider(provider_index);
      if (provider == nullptr) {
        add_issue(result, LockfileIssueCode::InvalidResolution, "permissions", "resolution contains an invalid provider index");
        continue;
      }
      permissions.insert(provider->permissions.begin(), provider->permissions.end());
    }

    std::vector<SerializedDependency> dependencies;
    for (const auto& dependency : resolution.dependencies()) {
      const auto capability = registry.capability_name(dependency.capability);
      const auto* provider = registry.provider(dependency.dependency);
      const auto* required_by = registry.provider(dependency.dependent);
      if (capability.empty() || provider == nullptr || required_by == nullptr) {
        add_issue(result, LockfileIssueCode::InvalidResolution, "dependencies", "resolution contains an invalid dependency edge");
        continue;
      }
      dependencies.push_back({std::string(capability), provider->id, required_by->id});
    }
    std::ranges::sort(dependencies, [](const SerializedDependency& left, const SerializedDependency& right) {
      return std::tie(left.capability, left.provider, left.required_by) < std::tie(right.capability, right.provider, right.required_by);
    });
    dependencies.erase(std::ranges::unique(dependencies, {},
                                           [](const SerializedDependency& dependency) {
                                             return std::tie(dependency.capability, dependency.provider, dependency.required_by);
                                           })
                           .begin(),
                       dependencies.end());

    if (!result.issues.empty()) return result;

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "schema: " << metadata.schema << '\n';
    output << "sdk: ";
    write_version(output, metadata.sdk);
    output << '\n';
    output << "target: " << target_name(metadata.target) << '\n';
    output << "profile: " << metadata.profile << '\n';

    if (permissions.empty()) {
      output << "permissions: []\n";
    } else {
      output << "permissions:\n";
      for (const auto& permission : permissions) output << "  - " << permission << '\n';
    }

    if (providers.empty()) {
      output << "resolved: {}\n";
    } else {
      output << "resolved:\n";
      for (const auto& provider : providers) {
        output << "  " << provider.capability << ":\n";
        output << "    provider: " << provider.provider << '\n';
        output << "    version: ";
        write_version(output, provider.version);
        output << '\n';
        output << "    linkage: " << linkage_name(provider.linkage) << '\n';
      }
    }

    if (dependencies.empty()) {
      output << "dependencies: []\n";
    } else {
      output << "dependencies:\n";
      for (const auto& dependency : dependencies) {
        output << "  - capability: " << dependency.capability << '\n';
        output << "    provider: " << dependency.provider << '\n';
        output << "    required-by: " << dependency.required_by << '\n';
      }
    }

    if (plugins.empty()) {
      output << "plugins: {}\n";
    } else {
      output << "plugins:\n";
      for (const auto& plugin : plugins) {
        output << "  " << plugin.provider << ":\n";
        output << "    version: ";
        write_version(output, plugin.version);
        output << '\n';
        output << "    abi: " << plugin.abi_version << '\n';
        output << "    package: ";
        write_yaml_string(output, plugin.package);
        output << '\n';
        output << "    hash: " << plugin.hash << '\n';
      }
    }

    result.contents = std::move(output).str();
    return result;
  }

}  // namespace mobagen::modules
