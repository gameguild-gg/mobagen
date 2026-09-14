#include "catalog_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::size_t max_catalog_collection_entries = 1024;
    constexpr std::size_t max_catalog_nodes = 32768;
    constexpr std::size_t max_catalog_depth = 32;

    struct MapEntry {
      std::string key;
      YAML::Node value;
    };

    class ModuleCatalogParser {
    public:
      ModuleCatalogParser(std::string_view source, std::string_view source_path) : source_(source), source_path_(source_path) {}

      CatalogParseResult parse() {
        if (source_.size() > max_module_catalog_bytes) {
          add_error(CatalogErrorCode::LimitExceeded, {}, {}, "module catalog exceeds the 1 MiB size limit");
          return finish();
        }

        YAML::Node root;
        try {
          const auto documents = YAML::LoadAll(std::string(source_));
          if (documents.size() != 1) {
            add_error(CatalogErrorCode::Syntax, {}, {}, "module catalog must contain exactly one YAML document");
            return finish();
          }
          root = documents.front();
        } catch (const YAML::Exception& error) {
          add_error(CatalogErrorCode::Syntax, error.mark, {}, error.msg);
          return finish();
        }

        reject_custom_tags(root, {}, 0);
        parse_root(root);
        return finish();
      }

    private:
      static bool is_implicit_tag(const std::string& tag) { return tag.empty() || tag == "?" || tag == "!"; }

      static std::string child_field(std::string_view parent, std::string_view child) {
        if (parent.empty()) return std::string(child);
        return std::string(parent) + '.' + std::string(child);
      }

      static bool is_allowed(std::string_view key, std::initializer_list<std::string_view> allowed) {
        return std::ranges::find(allowed, key) != allowed.end();
      }

      void add_error(CatalogErrorCode code, const YAML::Mark& mark, std::string field, std::string message) {
        const bool has_mark = !mark.is_null();
        errors_.push_back({
            .code = code,
            .source_path = source_path_,
            .line = has_mark ? static_cast<std::size_t>(mark.line + 1) : 0,
            .column = has_mark ? static_cast<std::size_t>(mark.column + 1) : 0,
            .field = std::move(field),
            .message = std::move(message),
        });
      }

      void remember_location(const std::string& field, const YAML::Mark& mark) { locations_.try_emplace(field, mark); }

      void reject_custom_tags(const YAML::Node& node, const std::string& field, std::size_t depth) {
        if (!node.IsDefined()) return;
        if (depth > max_catalog_depth || inspected_nodes_ >= max_catalog_nodes) {
          if (!node_limit_reported_) {
            add_error(CatalogErrorCode::LimitExceeded, node.Mark(), field, "YAML nesting or node count exceeds the catalog limit");
            node_limit_reported_ = true;
          }
          return;
        }
        ++inspected_nodes_;
        if (!is_implicit_tag(node.Tag())) {
          add_error(CatalogErrorCode::UnsupportedTag, node.Mark(), field, "custom YAML tags are not supported");
        }

        if (node.IsMap()) {
          for (const auto& pair : node) {
            std::string key = "<key>";
            if (pair.first.IsScalar()) key = pair.first.Scalar();
            const auto nested_field = child_field(field, key);
            if (!is_implicit_tag(pair.first.Tag())) {
              add_error(CatalogErrorCode::UnsupportedTag, pair.first.Mark(), nested_field, "custom YAML tags are not supported on mapping keys");
            }
            reject_custom_tags(pair.second, nested_field, depth + 1);
          }
        } else if (node.IsSequence()) {
          for (std::size_t index = 0; index < node.size(); ++index) {
            reject_custom_tags(node[index], field + '[' + std::to_string(index) + ']', depth + 1);
          }
        }
      }

      std::vector<MapEntry> read_map(const YAML::Node& node, const std::string& field,
                                     std::initializer_list<std::string_view> allowed_fields) {
        if (!node.IsMap()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected a mapping");
          return {};
        }
        if (node.size() > max_catalog_collection_entries) {
          add_error(CatalogErrorCode::LimitExceeded, node.Mark(), field, "mapping exceeds the 1024-entry catalog limit");
          return {};
        }

        std::vector<MapEntry> entries;
        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(CatalogErrorCode::WrongType, pair.first.Mark(), field, "mapping keys must be strings");
            continue;
          }
          std::string key = pair.first.Scalar();
          const auto nested_field = child_field(field, key);
          remember_location(nested_field, pair.first.Mark());
          if (!seen.insert(key).second) {
            add_error(CatalogErrorCode::DuplicateKey, pair.first.Mark(), nested_field, "mapping keys must be unique");
          }
          if (!is_allowed(key, allowed_fields)) {
            add_error(CatalogErrorCode::UnknownField, pair.first.Mark(), nested_field, "field is not part of catalog schema version 1");
          }
          entries.push_back({std::move(key), pair.second});
        }
        return entries;
      }

      const YAML::Node* find_entry(const std::vector<MapEntry>& entries, std::string_view key) const {
        const auto entry = std::ranges::find_if(entries, [=](const MapEntry& candidate) { return candidate.key == key; });
        return entry == entries.end() ? nullptr : &entry->value;
      }

      const YAML::Node* require_entry(const std::vector<MapEntry>& entries, std::string_view key, const std::string& field,
                                      const YAML::Mark& parent_mark) {
        if (const auto* node = find_entry(entries, key)) return node;
        add_error(CatalogErrorCode::MissingField, parent_mark, child_field(field, key), "required field is missing");
        return nullptr;
      }

      bool read_string(const YAML::Node& node, const std::string& field, std::string& output) {
        remember_location(field, node.Mark());
        if (!node.IsScalar()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected a string");
          return false;
        }
        output = node.Scalar();
        return true;
      }

      bool read_unsigned(const YAML::Node& node, const std::string& field, std::uint64_t& output) {
        remember_location(field, node.Mark());
        if (!node.IsScalar() || node.Tag() != "?") {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected an unquoted unsigned integer");
          return false;
        }
        const auto value = node.Scalar();
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), output);
        if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected an unsigned integer");
          return false;
        }
        return true;
      }

      bool read_schema(const YAML::Node& node) {
        std::uint64_t schema = 0;
        if (!read_unsigned(node, "schema", schema)) return false;
        if (schema > UINT32_MAX) {
          add_error(CatalogErrorCode::UnsupportedSchema, node.Mark(), "schema", "catalog schema version is outside the supported range");
          return false;
        }
        catalog_.schema = static_cast<std::uint32_t>(schema);
        if (catalog_.schema != module_catalog_schema_version) {
          add_error(CatalogErrorCode::UnsupportedSchema, node.Mark(), "schema", "only module catalog schema version 1 is supported");
          return false;
        }
        return true;
      }

      bool read_version(const YAML::Node& node, const std::string& field, SemanticVersion& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;
        const auto first = value.find('.');
        const auto second = first == std::string::npos ? std::string::npos : value.find('.', first + 1);
        if (first == std::string::npos || second == std::string::npos || value.find('.', second + 1) != std::string::npos) {
          add_error(CatalogErrorCode::InvalidValue, node.Mark(), field, "expected a major.minor.patch version");
          return false;
        }
        const auto parse_part = [&](std::string_view part, std::uint32_t& destination) {
          if (part.empty()) return false;
          const auto conversion = std::from_chars(part.data(), part.data() + part.size(), destination);
          return conversion.ec == std::errc{} && conversion.ptr == part.data() + part.size();
        };
        if (!parse_part(std::string_view(value).substr(0, first), output.major)
            || !parse_part(std::string_view(value).substr(first + 1, second - first - 1), output.minor)
            || !parse_part(std::string_view(value).substr(second + 1), output.patch)) {
          add_error(CatalogErrorCode::InvalidValue, node.Mark(), field, "expected a major.minor.patch version");
          return false;
        }
        return true;
      }

      void read_string_list(const YAML::Node& node, const std::string& field, std::vector<std::string>& output) {
        if (!node.IsSequence()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected a sequence");
          return;
        }
        if (node.size() > max_catalog_collection_entries) {
          add_error(CatalogErrorCode::LimitExceeded, node.Mark(), field, "sequence exceeds the 1024-entry catalog limit");
          return;
        }
        for (std::size_t index = 0; index < node.size(); ++index) {
          std::string value;
          if (read_string(node[index], field + '[' + std::to_string(index) + ']', value)) output.push_back(std::move(value));
        }
      }

      bool read_target(const YAML::Node& node, const std::string& field, TargetPlatform& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;
        static const std::map<std::string, TargetPlatform, std::less<>> targets{
            {"android", TargetPlatform::Android}, {"ios", TargetPlatform::IOS},       {"linux", TargetPlatform::Linux},
            {"macos", TargetPlatform::MacOS},     {"web", TargetPlatform::Web},     {"windows", TargetPlatform::Windows},
        };
        const auto target = targets.find(value);
        if (target == targets.end()) {
          add_error(CatalogErrorCode::InvalidValue, node.Mark(), field, "expected windows, linux, macos, web, android, or ios");
          return false;
        }
        output = target->second;
        return true;
      }

      bool read_linkage(const YAML::Node& node, const std::string& field, LinkageMode& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;
        static const std::map<std::string, LinkageMode, std::less<>> linkages{
            {"dynamic", LinkageMode::Dynamic}, {"process", LinkageMode::Process}, {"static", LinkageMode::Static}, {"wasm", LinkageMode::Wasm},
        };
        const auto linkage = linkages.find(value);
        if (linkage == linkages.end()) {
          add_error(CatalogErrorCode::InvalidValue, node.Mark(), field, "expected static, dynamic, wasm, or process");
          return false;
        }
        output = linkage->second;
        return true;
      }

      void read_reload(const YAML::Node& node, const std::string& field, ReloadPolicy& output) {
        std::string value;
        if (!read_string(node, field, value)) return;
        static const std::map<std::string, ReloadPolicy, std::less<>> policies{
            {"never", ReloadPolicy::Never}, {"restart", ReloadPolicy::Restart}, {"safe-point", ReloadPolicy::SafePoint}};
        const auto policy = policies.find(value);
        if (policy == policies.end()) {
          add_error(CatalogErrorCode::InvalidValue, node.Mark(), field, "expected never, restart, or safe-point");
          return;
        }
        output = policy->second;
      }

      static bool is_sha256(std::string_view value) {
        constexpr std::string_view prefix = "sha256:";
        return value.starts_with(prefix) && value.size() == prefix.size() + 64
               && std::ranges::all_of(value.substr(prefix.size()),
                                      [](char digit) { return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'); });
      }

      void parse_root(const YAML::Node& root) {
        const auto entries = read_map(root, {}, {"schema", "providers"});
        const auto* schema = require_entry(entries, "schema", {}, root.Mark());
        const auto* providers = require_entry(entries, "providers", {}, root.Mark());
        if (schema) read_schema(*schema);
        if (providers) parse_providers(*providers);
      }

      void parse_providers(const YAML::Node& node) {
        if (!node.IsMap()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), "providers", "expected a mapping");
          return;
        }
        if (node.size() > max_catalog_collection_entries) {
          add_error(CatalogErrorCode::LimitExceeded, node.Mark(), "providers", "provider count exceeds the 1024-entry catalog limit");
          return;
        }

        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(CatalogErrorCode::WrongType, pair.first.Mark(), "providers", "provider IDs must be strings");
            continue;
          }
          const std::string id = pair.first.Scalar();
          const std::string field = "providers." + id;
          remember_location(field, pair.first.Mark());
          if (!seen.insert(id).second) add_error(CatalogErrorCode::DuplicateKey, pair.first.Mark(), field, "provider IDs must be unique");
          parse_provider(pair.second, field, id);
        }
      }

      void parse_provider(const YAML::Node& node, const std::string& field, const std::string& id) {
        const auto entries = read_map(node, field,
                                      {"version", "provides", "requires", "optional", "conflicts", "reload", "configuration-schema",
                                       "permissions", "artifacts"});
        const auto* version = require_entry(entries, "version", field, node.Mark());
        const auto* provides = require_entry(entries, "provides", field, node.Mark());
        const auto* artifacts = require_entry(entries, "artifacts", field, node.Mark());

        PublishedProviderDescriptor published;
        published.provider.id = id;
        if (version) read_version(*version, field + ".version", published.provider.version);
        if (provides) read_string_list(*provides, field + ".provides", published.provider.provides);
        if (const auto* required = find_entry(entries, "requires")) {
          read_string_list(*required, field + ".requires", published.provider.required);
        }
        if (const auto* optional = find_entry(entries, "optional")) {
          read_string_list(*optional, field + ".optional", published.provider.optional);
        }
        if (const auto* conflicts = find_entry(entries, "conflicts")) {
          read_string_list(*conflicts, field + ".conflicts", published.provider.conflicts);
        }
        if (const auto* reload = find_entry(entries, "reload")) read_reload(*reload, field + ".reload", published.provider.reload);
        if (const auto* schema = find_entry(entries, "configuration-schema")) {
          read_string(*schema, field + ".configuration-schema", published.provider.configuration_schema);
        }
        if (const auto* permissions = find_entry(entries, "permissions")) {
          read_string_list(*permissions, field + ".permissions", published.provider.permissions);
        }
        if (artifacts) parse_artifacts(*artifacts, field + ".artifacts", published);
        append_provider_issues(field, published.provider);
        catalog_.providers.push_back(std::move(published));
      }

      void parse_artifacts(const YAML::Node& node, const std::string& field, PublishedProviderDescriptor& published) {
        if (!node.IsSequence()) {
          add_error(CatalogErrorCode::WrongType, node.Mark(), field, "expected a sequence");
          return;
        }
        if (node.size() == 0) {
          add_error(CatalogErrorCode::MissingField, node.Mark(), field, "provider must publish at least one artifact");
          return;
        }
        if (node.size() > max_catalog_collection_entries) {
          add_error(CatalogErrorCode::LimitExceeded, node.Mark(), field, "artifact count exceeds the 1024-entry catalog limit");
          return;
        }

        std::set<std::pair<TargetPlatform, LinkageMode>> seen;
        std::set<TargetPlatform> targets;
        std::set<LinkageMode> linkages;
        for (std::size_t index = 0; index < node.size(); ++index) {
          const std::string artifact_field = field + '[' + std::to_string(index) + ']';
          const auto entries = read_map(node[index], artifact_field, {"target", "linkage", "url", "size", "hash"});
          const auto* target = require_entry(entries, "target", artifact_field, node[index].Mark());
          const auto* linkage = require_entry(entries, "linkage", artifact_field, node[index].Mark());
          const auto* url = require_entry(entries, "url", artifact_field, node[index].Mark());
          const auto* size = require_entry(entries, "size", artifact_field, node[index].Mark());
          const auto* hash = require_entry(entries, "hash", artifact_field, node[index].Mark());

          ModuleArtifactDescriptor artifact;
          const bool target_ok = target && read_target(*target, artifact_field + ".target", artifact.target);
          const bool linkage_ok = linkage && read_linkage(*linkage, artifact_field + ".linkage", artifact.linkage);
          if (url && read_string(*url, artifact_field + ".url", artifact.url)
              && !is_secure_plugin_artifact_url(artifact.url)) {
            add_error(CatalogErrorCode::InvalidValue, url->Mark(), artifact_field + ".url",
                      "artifact URL must satisfy the secure HTTPS policy and name a .plugin package");
          }
          if (size && read_unsigned(*size, artifact_field + ".size", artifact.size)
              && (artifact.size == 0 || artifact.size > max_module_artifact_bytes)) {
            add_error(CatalogErrorCode::InvalidValue, size->Mark(), artifact_field + ".size", "artifact size must be between 1 byte and 512 MiB");
          }
          if (hash && read_string(*hash, artifact_field + ".hash", artifact.hash) && !is_sha256(artifact.hash)) {
            add_error(CatalogErrorCode::InvalidValue, hash->Mark(), artifact_field + ".hash", "expected sha256 followed by 64 lowercase hexadecimal digits");
          }
          if (target_ok && linkage_ok) {
            if (!seen.emplace(artifact.target, artifact.linkage).second) {
              add_error(CatalogErrorCode::DuplicateEntry, node[index].Mark(), artifact_field,
                        "provider artifacts must have unique target and linkage pairs");
            }
            if (targets.insert(artifact.target).second) published.provider.targets.push_back(artifact.target);
            if (linkages.insert(artifact.linkage).second) published.provider.linkages.push_back(artifact.linkage);
          }
          published.artifacts.push_back(std::move(artifact));
        }
      }

      void append_provider_issues(const std::string& field, const ProviderDescriptor& provider) {
        for (const auto& issue : validate(provider)) {
          std::string nested = issue.field;
          if (nested.starts_with("required")) nested.replace(0, std::string_view{"required"}.size(), "requires");
          if (nested.starts_with("configuration_schema")) {
            nested.replace(0, std::string_view{"configuration_schema"}.size(), "configuration-schema");
          }
          const auto full_field = child_field(field, nested);
          const auto location = locations_.find(full_field);
          CatalogErrorCode code = CatalogErrorCode::InvalidValue;
          if (issue.code == DescriptorIssueCode::MissingEntry) code = CatalogErrorCode::MissingField;
          if (issue.code == DescriptorIssueCode::DuplicateEntry) code = CatalogErrorCode::DuplicateEntry;
          if (issue.code == DescriptorIssueCode::LimitExceeded) code = CatalogErrorCode::LimitExceeded;
          add_error(code, location == locations_.end() ? YAML::Mark::null_mark() : location->second, full_field, issue.message);
        }
      }

      CatalogParseResult finish() {
        CatalogParseResult result{.errors = std::move(errors_)};
        if (result.errors.empty()) result.catalog = std::move(catalog_);
        return result;
      }

      std::string_view source_;
      std::string source_path_;
      ModuleCatalogDescriptor catalog_;
      std::vector<CatalogError> errors_;
      std::map<std::string, YAML::Mark, std::less<>> locations_;
      std::size_t inspected_nodes_{};
      bool node_limit_reported_{};
    };

  }  // namespace

  CatalogParseResult parse_module_catalog(std::string_view source, std::string_view source_path) {
    return ModuleCatalogParser(source, source_path).parse();
  }

}  // namespace mobagen::modules
