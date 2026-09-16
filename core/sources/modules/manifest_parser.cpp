#include "manifest_parser.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace mobagen::modules {

  namespace {

    struct MapEntry {
      std::string key;
      YAML::Node value;
    };

    class ProductManifestParser {
    public:
      ProductManifestParser(std::string_view source, std::string_view source_path) : source_(source), source_path_(source_path) {}

      ManifestParseResult parse() {
        if (source_.size() > max_product_manifest_bytes) {
          add_error(ManifestErrorCode::LimitExceeded, {}, {}, "mobagen.yaml exceeds the 1 MiB size limit");
          return finish();
        }

        YAML::Node root;
        try {
          const auto documents = YAML::LoadAll(std::string(source_));
          if (documents.size() != 1) {
            add_error(ManifestErrorCode::Syntax, {}, {}, "mobagen.yaml must contain exactly one YAML document");
            return finish();
          }
          root = documents.front();
        } catch (const YAML::Exception& error) {
          add_error(ManifestErrorCode::Syntax, error.mark, {}, error.msg);
          return finish();
        }

        reject_custom_tags(root, {}, 0);
        parse_root(root);
        append_descriptor_issues();
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

      static ManifestErrorCode manifest_code(DescriptorIssueCode code) {
        switch (code) {
          case DescriptorIssueCode::UnsupportedSchema:
            return ManifestErrorCode::UnsupportedSchema;
          case DescriptorIssueCode::MissingEntry:
            return ManifestErrorCode::MissingField;
          case DescriptorIssueCode::DuplicateEntry:
            return ManifestErrorCode::InvalidValue;
          case DescriptorIssueCode::InvalidIdentifier:
          case DescriptorIssueCode::InvalidCapability:
          case DescriptorIssueCode::InvalidPluginPath:
          case DescriptorIssueCode::InvalidSourceUrl:
          case DescriptorIssueCode::SelfDependency:
            return ManifestErrorCode::InvalidValue;
          case DescriptorIssueCode::LimitExceeded:
            return ManifestErrorCode::LimitExceeded;
        }
        return ManifestErrorCode::InvalidValue;
      }

      void add_error(ManifestErrorCode code, const YAML::Mark& mark, std::string field, std::string message) {
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
        if (depth > 32 || inspected_nodes_ >= 16384) {
          if (!node_limit_reported_) {
            add_error(ManifestErrorCode::LimitExceeded, node.Mark(), field, "YAML nesting or node count exceeds the manifest limit");
            node_limit_reported_ = true;
          }
          return;
        }
        ++inspected_nodes_;
        if (!is_implicit_tag(node.Tag())) {
          add_error(ManifestErrorCode::UnsupportedTag, node.Mark(), field, "custom YAML tags are not supported");
        }

        if (node.IsMap()) {
          for (const auto& pair : node) {
            std::string key = "<key>";
            if (pair.first.IsScalar()) key = pair.first.Scalar();
            const auto nested_field = child_field(field, key);
            if (!is_implicit_tag(pair.first.Tag())) {
              add_error(ManifestErrorCode::UnsupportedTag, pair.first.Mark(), nested_field, "custom YAML tags are not supported on mapping keys");
            }
            reject_custom_tags(pair.second, nested_field, depth + 1);
          }
        } else if (node.IsSequence()) {
          for (std::size_t index = 0; index < node.size(); ++index) {
            reject_custom_tags(node[index], field + '[' + std::to_string(index) + ']', depth + 1);
          }
        }
      }

      std::vector<MapEntry> read_map(const YAML::Node& node, const std::string& field, std::initializer_list<std::string_view> allowed_fields) {
        if (!node.IsMap()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), field, "expected a mapping");
          return {};
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), field, "mapping exceeds the 1024-entry manifest limit");
          return {};
        }

        std::vector<MapEntry> entries;
        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(ManifestErrorCode::WrongType, pair.first.Mark(), field, "mapping keys must be strings");
            continue;
          }

          std::string key = pair.first.Scalar();
          const std::string nested_field = child_field(field, key);
          remember_location(nested_field, pair.first.Mark());
          if (!seen.insert(key).second) {
            add_error(ManifestErrorCode::DuplicateKey, pair.first.Mark(), nested_field, "mapping keys must be unique");
          }
          if (!is_allowed(key, allowed_fields)) {
            add_error(ManifestErrorCode::UnknownField, pair.first.Mark(), nested_field, "field is not part of schema version 1");
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
        add_error(ManifestErrorCode::MissingField, parent_mark, child_field(field, key), "required field is missing");
        return nullptr;
      }

      bool read_string(const YAML::Node& node, const std::string& field, std::string& output) {
        remember_location(field, node.Mark());
        if (!node.IsScalar()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), field, "expected a string");
          return false;
        }
        output = node.Scalar();
        return true;
      }

      bool read_schema(const YAML::Node& node) {
        remember_location("schema", node.Mark());
        if (!node.IsScalar() || node.Tag() != "?") {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "schema", "expected an unquoted integer");
          return false;
        }

        const auto value = node.Scalar();
        std::uint32_t schema = 0;
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), schema);
        if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "schema", "expected an unsigned integer");
          return false;
        }
        descriptor_.schema = schema;
        return true;
      }

      bool read_bool(const YAML::Node& node, const std::string& field, bool& output) {
        remember_location(field, node.Mark());
        if (!node.IsScalar() || node.Tag() != "?") {
          add_error(ManifestErrorCode::WrongType, node.Mark(), field, "expected an unquoted boolean");
          return false;
        }
        if (node.Scalar() == "true") {
          output = true;
          return true;
        }
        if (node.Scalar() == "false") {
          output = false;
          return true;
        }
        add_error(ManifestErrorCode::InvalidValue, node.Mark(), field, "expected true or false");
        return false;
      }

      bool read_linkage(const YAML::Node& node, const std::string& field, LinkageMode& output) {
        std::string value;
        if (!read_string(node, field, value)) return false;

        static const std::map<std::string, LinkageMode, std::less<>> linkages{
            {"dynamic", LinkageMode::Dynamic},
            {"process", LinkageMode::Process},
            {"static", LinkageMode::Static},
            {"wasm", LinkageMode::Wasm},
        };
        const auto linkage = linkages.find(value);
        if (linkage == linkages.end()) {
          add_error(ManifestErrorCode::InvalidValue, node.Mark(), field, "expected static, dynamic, wasm, or process");
          return false;
        }
        output = linkage->second;
        return true;
      }

      void parse_root(const YAML::Node& root) {
        const auto entries = read_map(root, {}, {"schema", "name", "sources", "modules", "plugins", "profiles"});
        const auto* schema = require_entry(entries, "schema", {}, root.Mark());
        const auto* name = require_entry(entries, "name", {}, root.Mark());
        const auto* modules = require_entry(entries, "modules", {}, root.Mark());

        if (schema) read_schema(*schema);
        if (name) read_string(*name, "name", descriptor_.name);
        if (const auto* sources = find_entry(entries, "sources")) parse_sources(*sources);
        if (modules) parse_modules(*modules);
        if (const auto* plugins = find_entry(entries, "plugins")) parse_plugins(*plugins);
        if (const auto* profiles = find_entry(entries, "profiles")) parse_profiles(*profiles);
      }

      void parse_sources(const YAML::Node& node) {
        if (!node.IsMap()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "sources", "expected a mapping");
          return;
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), "sources", "source count exceeds the 1024-entry manifest limit");
          return;
        }

        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(ManifestErrorCode::WrongType, pair.first.Mark(), "sources", "source names must be strings");
            continue;
          }
          const std::string name = pair.first.Scalar();
          const std::string field = "sources." + name;
          remember_location(field, pair.first.Mark());
          if (!seen.insert(name).second) {
            add_error(ManifestErrorCode::DuplicateKey, pair.first.Mark(), field, "source names must be unique");
          }

          const auto source_entries = read_map(pair.second, field, {"url"});
          const auto* url = require_entry(source_entries, "url", field, pair.second.Mark());
          std::string parsed_url;
          if (url && read_string(*url, field + ".url", parsed_url)) {
            descriptor_.sources.push_back({name, std::move(parsed_url)});
          }
        }
      }

      void parse_modules(const YAML::Node& node) {
        if (!node.IsMap()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "modules", "expected a mapping");
          return;
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), "modules", "module count exceeds the 1024-entry manifest limit");
          return;
        }

        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(ManifestErrorCode::WrongType, pair.first.Mark(), "modules", "module aliases must be strings");
            continue;
          }
          const std::string alias = pair.first.Scalar();
          const std::string field = "modules." + alias;
          remember_location(field, pair.first.Mark());
          if (!seen.insert(alias).second) {
            add_error(ManifestErrorCode::DuplicateKey, pair.first.Mark(), field, "module aliases must be unique");
          }

          const auto module_entries = read_map(pair.second, field, {"capability", "use", "config"});
          const auto* use = require_entry(module_entries, "use", field, pair.second.Mark());
          std::string provider;
          if (use && read_string(*use, field + ".use", provider)) {
            std::string capability;
            if (const auto* capability_node = find_entry(module_entries, "capability")) {
              read_string(*capability_node, field + ".capability", capability);
            }
            std::optional<ModuleConfiguration> configuration;
            if (const auto* config = find_entry(module_entries, "config")) {
              const auto config_field = field + ".config";
              const auto config_entries = read_map(*config, config_field, {"schema", "data"});
              const auto* schema = require_entry(config_entries, "schema", config_field, config->Mark());
              const auto* data = require_entry(config_entries, "data", config_field, config->Mark());
              ModuleConfiguration parsed;
              if (schema && data && read_string(*schema, config_field + ".schema", parsed.schema)
                  && read_string(*data, config_field + ".data", parsed.data)) {
                configuration = std::move(parsed);
              }
            }
            descriptor_.modules.push_back({
                .alias = alias,
                .provider = std::move(provider),
                .configuration = std::move(configuration),
                .capability = std::move(capability),
            });
          }
        }
      }

      void parse_plugins(const YAML::Node& node) {
        if (!node.IsSequence()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "plugins", "expected a sequence");
          return;
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), "plugins", "plugin count exceeds the 1024-entry manifest limit");
          return;
        }
        for (std::size_t index = 0; index < node.size(); ++index) {
          std::string path;
          const std::string field = "plugins[" + std::to_string(index) + ']';
          if (read_string(node[index], field, path)) {
            descriptor_.plugins.push_back(std::move(path));
          }
        }
      }

      void parse_permissions(const YAML::Node& node, const std::string& field, std::vector<std::string>& output) {
        if (!node.IsSequence()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), field, "expected a sequence");
          return;
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), field, "permission count exceeds the 1024-entry manifest limit");
          return;
        }
        for (std::size_t index = 0; index < node.size(); ++index) {
          std::string permission;
          if (read_string(node[index], field + '[' + std::to_string(index) + ']', permission)) {
            output.push_back(std::move(permission));
          }
        }
      }

      void parse_profiles(const YAML::Node& node) {
        if (!node.IsMap()) {
          add_error(ManifestErrorCode::WrongType, node.Mark(), "profiles", "expected a mapping");
          return;
        }
        if (node.size() > max_manifest_collection_entries) {
          add_error(ManifestErrorCode::LimitExceeded, node.Mark(), "profiles", "profile count exceeds the 1024-entry manifest limit");
          return;
        }

        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(ManifestErrorCode::WrongType, pair.first.Mark(), "profiles", "profile names must be strings");
            continue;
          }
          const std::string name = pair.first.Scalar();
          const std::string field = "profiles." + name;
          remember_location(field, pair.first.Mark());
          if (!seen.insert(name).second) {
            add_error(ManifestErrorCode::DuplicateKey, pair.first.Mark(), field, "profile names must be unique");
          }

          const auto profile_entries = read_map(pair.second, field, {"linkage", "editor", "permissions"});
          const auto* linkage = require_entry(profile_entries, "linkage", field, pair.second.Mark());
          ProfileDescriptor profile{.name = name};
          if (linkage) read_linkage(*linkage, field + ".linkage", profile.linkage);
          if (const auto* editor = find_entry(profile_entries, "editor")) {
            read_bool(*editor, field + ".editor", profile.editor);
          }
          if (const auto* permissions = find_entry(profile_entries, "permissions")) {
            parse_permissions(*permissions, field + ".permissions", profile.permissions);
          }
          descriptor_.profiles.push_back(std::move(profile));
        }
      }

      bool already_reported(std::string_view field) const {
        return std::ranges::any_of(errors_, [=](const ManifestError& error) { return error.field == field; });
      }

      void append_descriptor_issues() {
        for (const auto& issue : validate(descriptor_)) {
          if (already_reported(issue.field)) continue;
          const auto location = locations_.find(issue.field);
          add_error(manifest_code(issue.code), location == locations_.end() ? YAML::Mark::null_mark() : location->second, issue.field, issue.message);
        }
      }

      ManifestParseResult finish() {
        ManifestParseResult result{.errors = std::move(errors_)};
        if (result.errors.empty()) result.descriptor = std::move(descriptor_);
        return result;
      }

      std::string_view source_;
      std::string source_path_;
      ProductDescriptor descriptor_;
      std::vector<ManifestError> errors_;
      std::map<std::string, YAML::Mark, std::less<>> locations_;
      std::size_t inspected_nodes_{};
      bool node_limit_reported_{};
    };

  }  // namespace

  ManifestParseResult parse_product_manifest(std::string_view source, std::string_view source_path) {
    return ProductManifestParser(source, source_path).parse();
  }

}  // namespace mobagen::modules
