#include "lockfile.hpp"

#include "assets/asset_id.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::size_t max_lockfile_collection_entries = 4096;
    constexpr std::size_t max_lockfile_nodes = 65536;
    constexpr std::size_t max_lockfile_depth = 32;

    struct MapEntry {
      std::string key;
      YAML::Node value;
    };

    class LockfileParser {
    public:
      LockfileParser(std::string_view source, std::string_view source_path)
          : source_(source), source_path_(source_path) {}

      LockfileParseResult parse() {
        if (source_.size() > max_lockfile_bytes) {
          add_issue(LockfileParseIssueCode::LimitExceeded, {}, {},
                    "lockfile exceeds the 1 MiB size limit");
          return finish();
        }

        YAML::Node root;
        try {
          const auto documents = YAML::LoadAll(std::string{source_});
          if (documents.size() != 1) {
            add_issue(LockfileParseIssueCode::Syntax, {}, {},
                      "lockfile must contain exactly one YAML document");
            return finish();
          }
          root = documents.front();
        } catch (const YAML::Exception& exception) {
          add_issue(LockfileParseIssueCode::Syntax, exception.mark, {}, exception.msg);
          return finish();
        }

        reject_custom_tags(root, {}, 0);
        parse_root(root);
        return finish();
      }

    private:
      static bool is_implicit_tag(const std::string& tag) {
        return tag.empty() || tag == "?" || tag == "!";
      }

      static std::string child_field(std::string_view parent, std::string_view child) {
        return parent.empty() ? std::string{child}
                              : std::string{parent} + '.' + std::string{child};
      }

      static bool is_allowed(std::string_view key,
                             std::initializer_list<std::string_view> allowed) {
        return std::ranges::find(allowed, key) != allowed.end();
      }

      static bool is_hash(std::string_view value) {
        return assets::parse_asset_id(value).has_value();
      }

      static bool is_portable_package_path(std::string_view value) {
        if (value.empty() || value.contains('\\')) return false;
        const std::filesystem::path path{value};
        if (path.is_absolute() || path.has_root_path() || path.extension() != ".plugin"
            || path.lexically_normal().generic_string() != value) {
          return false;
        }
        for (const auto& component : path) {
          const auto text = component.generic_string();
          if (text.empty() || text == "." || text == ".."
              || !std::ranges::all_of(text, [](unsigned char value_char) {
                   return value_char >= 0x20U && value_char != 0x7fU;
                 })) {
            return false;
          }
        }
        return true;
      }

      void add_issue(LockfileParseIssueCode code, const YAML::Mark& mark, std::string field,
                     std::string message) {
        const auto marked = !mark.is_null();
        issues_.push_back({
            .code = code,
            .source_path = source_path_,
            .line = marked ? static_cast<std::size_t>(mark.line + 1) : 0,
            .column = marked ? static_cast<std::size_t>(mark.column + 1) : 0,
            .field = std::move(field),
            .message = std::move(message),
        });
      }

      void reject_custom_tags(const YAML::Node& node, const std::string& field,
                              std::size_t depth) {
        if (!node.IsDefined()) return;
        if (depth > max_lockfile_depth || inspected_nodes_ >= max_lockfile_nodes) {
          if (!node_limit_reported_) {
            add_issue(LockfileParseIssueCode::LimitExceeded, node.Mark(), field,
                      "YAML nesting or node count exceeds the lockfile limit");
            node_limit_reported_ = true;
          }
          return;
        }
        ++inspected_nodes_;
        if (!is_implicit_tag(node.Tag())) {
          add_issue(LockfileParseIssueCode::UnsupportedTag, node.Mark(), field,
                    "custom YAML tags are not supported");
        }
        if (node.IsMap()) {
          for (const auto& pair : node) {
            std::string key = "<key>";
            if (pair.first.IsScalar()) key = pair.first.Scalar();
            const auto nested = child_field(field, key);
            if (!is_implicit_tag(pair.first.Tag())) {
              add_issue(LockfileParseIssueCode::UnsupportedTag, pair.first.Mark(), nested,
                        "custom YAML tags are not supported on mapping keys");
            }
            reject_custom_tags(pair.second, nested, depth + 1);
          }
        } else if (node.IsSequence()) {
          for (std::size_t index = 0; index < node.size(); ++index) {
            reject_custom_tags(node[index], field + '[' + std::to_string(index) + ']',
                               depth + 1);
          }
        }
      }

      std::vector<MapEntry> read_map(
          const YAML::Node& node, const std::string& field,
          std::initializer_list<std::string_view> allowed = {}
      ) {
        if (!node.IsMap()) {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), field,
                    "expected a mapping");
          return {};
        }
        if (node.size() > max_lockfile_collection_entries) {
          add_issue(LockfileParseIssueCode::LimitExceeded, node.Mark(), field,
                    "mapping exceeds the 4096-entry lockfile limit");
          return {};
        }
        std::vector<MapEntry> entries;
        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_issue(LockfileParseIssueCode::WrongType, pair.first.Mark(), field,
                      "mapping keys must be strings");
            continue;
          }
          std::string key = pair.first.Scalar();
          const auto nested = child_field(field, key);
          if (!seen.insert(key).second) {
            add_issue(LockfileParseIssueCode::DuplicateKey, pair.first.Mark(), nested,
                      "mapping keys must be unique");
          }
          if (allowed.size() != 0 && !is_allowed(key, allowed)) {
            add_issue(LockfileParseIssueCode::UnknownField, pair.first.Mark(), nested,
                      "field is not part of lockfile schema version 1");
          }
          entries.push_back({std::move(key), pair.second});
        }
        return entries;
      }

      static const YAML::Node* find_entry(const std::vector<MapEntry>& entries,
                                          std::string_view key) {
        const auto found = std::ranges::find_if(entries, [=](const MapEntry& entry) {
          return entry.key == key;
        });
        return found == entries.end() ? nullptr : &found->value;
      }

      const YAML::Node* require_entry(const std::vector<MapEntry>& entries,
                                      std::string_view key, const std::string& field,
                                      const YAML::Mark& mark) {
        if (const auto* node = find_entry(entries, key)) return node;
        add_issue(LockfileParseIssueCode::MissingField, mark, child_field(field, key),
                  "required field is missing");
        return nullptr;
      }

      bool read_string(const YAML::Node& node, const std::string& field,
                       std::string& output) {
        if (!node.IsScalar()) {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), field,
                    "expected a string");
          return false;
        }
        output = node.Scalar();
        return true;
      }

      bool read_unsigned(const YAML::Node& node, const std::string& field,
                         std::uint64_t& output) {
        if (!node.IsScalar() || node.Tag() != "?") {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), field,
                    "expected an unquoted unsigned integer");
          return false;
        }
        const auto value = node.Scalar();
        const auto converted = std::from_chars(value.data(), value.data() + value.size(), output);
        if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size()) {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), field,
                    "expected an unsigned integer");
          return false;
        }
        return true;
      }

      bool read_version(const YAML::Node& node, const std::string& field,
                        SemanticVersion& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;
        const auto first = value.find('.');
        const auto second = first == std::string::npos ? std::string::npos
                                                       : value.find('.', first + 1);
        const auto parse_part = [](std::string_view part, std::uint32_t& destination) {
          if (part.empty()) return false;
          const auto converted = std::from_chars(part.data(), part.data() + part.size(),
                                                 destination);
          return converted.ec == std::errc{} && converted.ptr == part.data() + part.size();
        };
        if (first == std::string::npos || second == std::string::npos
            || value.find('.', second + 1) != std::string::npos
            || !parse_part(std::string_view{value}.substr(0, first), output.major)
            || !parse_part(std::string_view{value}.substr(first + 1, second - first - 1),
                           output.minor)
            || !parse_part(std::string_view{value}.substr(second + 1), output.patch)) {
          add_issue(LockfileParseIssueCode::InvalidValue, node.Mark(), field,
                    "expected a major.minor.patch version");
          return false;
        }
        return true;
      }

      bool read_target(const YAML::Node& node, TargetPlatform& output) {
        std::string value;
        if (!read_string(node, "target", value)) return false;
        static const std::map<std::string, TargetPlatform, std::less<>> values{
            {"android", TargetPlatform::Android}, {"ios", TargetPlatform::IOS},
            {"linux", TargetPlatform::Linux},     {"macos", TargetPlatform::MacOS},
            {"web", TargetPlatform::Web},         {"windows", TargetPlatform::Windows},
        };
        const auto found = values.find(value);
        if (found == values.end()) {
          add_issue(LockfileParseIssueCode::InvalidValue, node.Mark(), "target",
                    "target is not supported by lockfile schema version 1");
          return false;
        }
        output = found->second;
        return true;
      }

      bool read_linkage(const YAML::Node& node, const std::string& field,
                        LinkageMode& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;
        static const std::map<std::string, LinkageMode, std::less<>> values{
            {"dynamic", LinkageMode::Dynamic}, {"process", LinkageMode::Process},
            {"static", LinkageMode::Static},   {"wasm", LinkageMode::Wasm},
        };
        const auto found = values.find(value);
        if (found == values.end()) {
          add_issue(LockfileParseIssueCode::InvalidValue, node.Mark(), field,
                    "expected static, dynamic, wasm, or process");
          return false;
        }
        output = found->second;
        return true;
      }

      void parse_root(const YAML::Node& root) {
        const auto entries = read_map(
            root, {}, {"schema", "sdk", "target", "profile", "manifest", "permissions",
                       "configurations", "resolved", "dependencies", "plugins"}
        );
        const auto* schema = require_entry(entries, "schema", {}, root.Mark());
        const auto* sdk = require_entry(entries, "sdk", {}, root.Mark());
        const auto* target = require_entry(entries, "target", {}, root.Mark());
        const auto* profile = require_entry(entries, "profile", {}, root.Mark());
        const auto* manifest = require_entry(entries, "manifest", {}, root.Mark());
        const auto* permissions = require_entry(entries, "permissions", {}, root.Mark());
        const auto* configurations = require_entry(entries, "configurations", {}, root.Mark());
        const auto* resolved = require_entry(entries, "resolved", {}, root.Mark());
        const auto* dependencies = require_entry(entries, "dependencies", {}, root.Mark());
        const auto* plugins = require_entry(entries, "plugins", {}, root.Mark());

        std::uint64_t schema_value = 0;
        if (schema && read_unsigned(*schema, "schema", schema_value)) {
          if (schema_value != lockfile_schema_version) {
            add_issue(LockfileParseIssueCode::UnsupportedSchema, schema->Mark(), "schema",
                      "only lockfile schema version 1 is supported");
          } else {
            document_.metadata.schema = static_cast<std::uint32_t>(schema_value);
          }
        }
        if (sdk) read_version(*sdk, "sdk", document_.metadata.sdk);
        if (target) read_target(*target, document_.metadata.target);
        if (profile && read_string(*profile, "profile", document_.metadata.profile)
            && !is_slug(document_.metadata.profile)) {
          add_issue(LockfileParseIssueCode::InvalidValue, profile->Mark(), "profile",
                    "expected a lowercase profile slug");
        }
        if (manifest
            && read_string(*manifest, "manifest", document_.metadata.manifest_hash)
            && !is_hash(document_.metadata.manifest_hash)) {
          add_issue(LockfileParseIssueCode::InvalidHash, manifest->Mark(), "manifest",
                    "manifest hash is not canonical SHA-256");
        }
        if (permissions) parse_permissions(*permissions);
        if (configurations) parse_configurations(*configurations);
        if (resolved) parse_resolved(*resolved);
        if (dependencies) parse_dependencies(*dependencies);
        if (plugins) parse_plugins(*plugins);
      }

      void parse_permissions(const YAML::Node& node) {
        if (!node.IsSequence()) {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), "permissions",
                    "expected a sequence");
          return;
        }
        if (node.size() > max_lockfile_collection_entries) {
          add_issue(LockfileParseIssueCode::LimitExceeded, node.Mark(), "permissions",
                    "permission count exceeds the lockfile limit");
          return;
        }
        std::set<std::string> seen;
        for (std::size_t index = 0; index < node.size(); ++index) {
          std::string permission;
          const auto field = "permissions[" + std::to_string(index) + ']';
          if (!read_string(node[index], field, permission)) continue;
          if (!is_slug(permission)) {
            add_issue(LockfileParseIssueCode::InvalidValue, node[index].Mark(), field,
                      "permission must be a lowercase slug");
          } else if (!seen.insert(permission).second) {
            add_issue(LockfileParseIssueCode::DuplicateEntry, node[index].Mark(), field,
                      "permissions must be unique");
          }
          document_.permissions.push_back(std::move(permission));
        }
      }

      void parse_configurations(const YAML::Node& node) {
        const auto providers = read_map(node, "configurations");
        for (const auto& provider : providers) {
          const auto field = "configurations." + provider.key;
          const auto entries = read_map(provider.value, field, {"schema", "hash"});
          const auto* schema = require_entry(entries, "schema", field, provider.value.Mark());
          const auto* hash = require_entry(entries, "hash", field, provider.value.Mark());
          LockedConfiguration configuration{.provider = provider.key};
          if (!is_provider_id(provider.key)) {
            add_issue(LockfileParseIssueCode::InvalidValue, provider.value.Mark(), field,
                      "configuration key must be a provider ID");
          }
          if (schema && read_string(*schema, field + ".schema", configuration.schema)
              && !is_capability_id(configuration.schema)) {
            add_issue(LockfileParseIssueCode::InvalidValue, schema->Mark(), field + ".schema",
                      "configuration schema must be a versioned dotted ID");
          }
          if (hash && read_string(*hash, field + ".hash", configuration.hash)
              && !is_hash(configuration.hash)) {
            add_issue(LockfileParseIssueCode::InvalidHash, hash->Mark(), field + ".hash",
                      "configuration hash is not canonical SHA-256");
          }
          document_.configurations.push_back(std::move(configuration));
        }
      }

      void parse_resolved(const YAML::Node& node) {
        const auto capabilities = read_map(node, "resolved");
        for (const auto& capability : capabilities) {
          const auto field = "resolved." + capability.key;
          const auto entries = read_map(capability.value, field,
                                        {"provider", "version", "linkage"});
          const auto* provider = require_entry(entries, "provider", field, capability.value.Mark());
          const auto* version = require_entry(entries, "version", field, capability.value.Mark());
          const auto* linkage = require_entry(entries, "linkage", field, capability.value.Mark());
          LockedProviderSelection selection{.capability = capability.key};
          if (!is_capability_id(capability.key)) {
            add_issue(LockfileParseIssueCode::InvalidValue, capability.value.Mark(), field,
                      "resolved key must be a versioned capability ID");
          }
          if (provider && read_string(*provider, field + ".provider", selection.provider)
              && !is_provider_id(selection.provider)) {
            add_issue(LockfileParseIssueCode::InvalidValue, provider->Mark(), field + ".provider",
                      "resolved provider must be a dotted provider ID");
          }
          if (version) read_version(*version, field + ".version", selection.version);
          if (linkage) read_linkage(*linkage, field + ".linkage", selection.linkage);
          document_.resolved.push_back(std::move(selection));
        }
      }

      void parse_dependencies(const YAML::Node& node) {
        if (!node.IsSequence()) {
          add_issue(LockfileParseIssueCode::WrongType, node.Mark(), "dependencies",
                    "expected a sequence");
          return;
        }
        if (node.size() > max_lockfile_collection_entries) {
          add_issue(LockfileParseIssueCode::LimitExceeded, node.Mark(), "dependencies",
                    "dependency count exceeds the lockfile limit");
          return;
        }
        std::set<std::tuple<std::string, std::string, std::string>> seen;
        for (std::size_t index = 0; index < node.size(); ++index) {
          const auto field = "dependencies[" + std::to_string(index) + ']';
          const auto entries = read_map(node[index], field,
                                        {"capability", "provider", "required-by"});
          const auto* capability = require_entry(entries, "capability", field, node[index].Mark());
          const auto* provider = require_entry(entries, "provider", field, node[index].Mark());
          const auto* required_by = require_entry(entries, "required-by", field, node[index].Mark());
          LockedDependency dependency;
          if (capability) read_string(*capability, field + ".capability", dependency.capability);
          if (provider) read_string(*provider, field + ".provider", dependency.provider);
          if (required_by) read_string(*required_by, field + ".required-by", dependency.required_by);
          if (!is_capability_id(dependency.capability) || !is_provider_id(dependency.provider)
              || !is_provider_id(dependency.required_by)) {
            add_issue(LockfileParseIssueCode::InvalidValue, node[index].Mark(), field,
                      "dependency IDs are invalid");
          } else if (!seen.emplace(dependency.capability, dependency.provider,
                                   dependency.required_by).second) {
            add_issue(LockfileParseIssueCode::DuplicateEntry, node[index].Mark(), field,
                      "dependencies must be unique");
          }
          document_.dependencies.push_back(std::move(dependency));
        }
      }

      void parse_plugins(const YAML::Node& node) {
        const auto providers = read_map(node, "plugins");
        for (const auto& provider : providers) {
          const auto field = "plugins." + provider.key;
          const auto entries = read_map(provider.value, field,
                                        {"version", "abi", "package", "hash"});
          const auto* version = require_entry(entries, "version", field, provider.value.Mark());
          const auto* abi = require_entry(entries, "abi", field, provider.value.Mark());
          const auto* package = require_entry(entries, "package", field, provider.value.Mark());
          const auto* hash = require_entry(entries, "hash", field, provider.value.Mark());
          PluginLockEntry plugin{.provider = provider.key};
          if (!is_provider_id(provider.key)) {
            add_issue(LockfileParseIssueCode::InvalidValue, provider.value.Mark(), field,
                      "plugin key must be a dotted provider ID");
          }
          if (version) read_version(*version, field + ".version", plugin.version);
          std::uint64_t abi_version = 0;
          if (abi && read_unsigned(*abi, field + ".abi", abi_version)) {
            if (abi_version == 0 || abi_version > UINT32_MAX) {
              add_issue(LockfileParseIssueCode::InvalidValue, abi->Mark(), field + ".abi",
                        "plugin ABI version must be positive and fit in 32 bits");
            } else {
              plugin.abi_version = static_cast<std::uint32_t>(abi_version);
            }
          }
          if (package && read_string(*package, field + ".package", plugin.package)
              && !is_portable_package_path(plugin.package)) {
            add_issue(LockfileParseIssueCode::InvalidValue, package->Mark(), field + ".package",
                      "plugin package must be a portable relative .plugin path");
          }
          if (hash && read_string(*hash, field + ".hash", plugin.hash)
              && !is_hash(plugin.hash)) {
            add_issue(LockfileParseIssueCode::InvalidHash, hash->Mark(), field + ".hash",
                      "plugin hash is not canonical SHA-256");
          }
          document_.metadata.plugins.push_back(std::move(plugin));
        }
      }

      LockfileParseResult finish() {
        LockfileParseResult result{.issues = std::move(issues_)};
        if (result.issues.empty()) result.document = std::move(document_);
        return result;
      }

      std::string_view source_;
      std::string source_path_;
      LockfileDocument document_;
      std::vector<LockfileParseIssue> issues_;
      std::size_t inspected_nodes_{};
      bool node_limit_reported_{};
    };

  }  // namespace

  LockfileParseResult parse_lockfile(std::string_view source, std::string_view source_path) {
    return LockfileParser{source, source_path}.parse();
  }

}  // namespace mobagen::modules
