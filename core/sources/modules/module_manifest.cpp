#include "module_manifest.hpp"

#include "assets/asset_id.hpp"
#include "descriptor.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <map>
#include <ostream>
#include <ranges>
#include <set>
#include <sstream>
#include <span>
#include <utility>

namespace mobagen::modules {
  namespace {

    constexpr std::size_t max_module_manifest_depth = 32;
    constexpr std::size_t max_module_manifest_nodes = 16384;
    constexpr std::size_t max_module_manifest_entries = 1024;

    [[nodiscard]] bool marshaling_type_name(ModuleMarshalingType type, std::string_view& name) {
      switch (type) {
        case ModuleMarshalingType::Void:
          name = "void";
          return true;
        case ModuleMarshalingType::I32:
          name = "i32";
          return true;
        case ModuleMarshalingType::I64:
          name = "i64";
          return true;
        case ModuleMarshalingType::F32:
          name = "f32";
          return true;
        case ModuleMarshalingType::F64:
          name = "f64";
          return true;
        case ModuleMarshalingType::Ptr:
          name = "ptr";
          return true;
        case ModuleMarshalingType::Span:
          name = "span";
          return true;
      }
      return false;
    }

    [[nodiscard]] std::optional<ModuleMarshalingType> marshaling_type_from_name(std::string_view name) {
      static constexpr std::pair<std::string_view, ModuleMarshalingType> names[]{
          {"void", ModuleMarshalingType::Void},   {"i32", ModuleMarshalingType::I32},   {"i64", ModuleMarshalingType::I64},
          {"f32", ModuleMarshalingType::F32},     {"f64", ModuleMarshalingType::F64},   {"ptr", ModuleMarshalingType::Ptr},
          {"span", ModuleMarshalingType::Span},
      };
      for (const auto& [candidate, type] : names) {
        if (name == candidate) return type;
      }
      return std::nullopt;
    }

    struct MapEntry {
      std::string key;
      YAML::Node value;
    };

    class ModuleManifestParser {
    public:
      ModuleManifestParser(std::string_view source, std::string_view source_path) : source_(source), source_path_(source_path) {}

      ModuleManifestParseResult parse() {
        if (source_.size() > max_module_manifest_bytes) {
          add_error(ModuleManifestErrorCode::LimitExceeded, {}, {}, "module manifest exceeds the 1 MiB size limit");
          return finish();
        }

        YAML::Node root;
        try {
          const auto documents = YAML::LoadAll(std::string{source_});
          if (documents.size() != 1) {
            add_error(ModuleManifestErrorCode::Syntax, {}, {}, "module manifest must contain exactly one YAML document");
            return finish();
          }
          root = documents.front();
        } catch (const YAML::Exception& error) {
          add_error(ModuleManifestErrorCode::Syntax, error.mark, {}, error.msg);
          return finish();
        }

        reject_custom_tags(root, {}, 0);
        parse_root(root);
        return finish();
      }

    private:
      static bool is_implicit_tag(const std::string& tag) { return tag.empty() || tag == "?" || tag == "!"; }

      static std::string child_field(std::string_view parent, std::string_view child) {
        return parent.empty() ? std::string{child} : std::string{parent} + '.' + std::string{child};
      }

      static bool is_allowed(std::string_view key, std::initializer_list<std::string_view> allowed) {
        return std::ranges::find(allowed, key) != allowed.end();
      }

      static bool is_sha256(std::string_view value) {
        constexpr std::string_view prefix = "sha256:";
        return value.starts_with(prefix) && value.size() == prefix.size() + 64 && std::ranges::all_of(value.substr(prefix.size()), [](char digit) {
                 return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
               });
      }

      static bool is_valid_signature_id(std::uint64_t value) {
        if (value == 0 || value > UINT32_MAX) return false;
        /* Full descriptor decodability (todo 10): the id must decode into a
         * marshaling descriptor the dispatchers can walk — magic, param
         * count, and every type code. Undecodable ids are rejected HERE, at
         * load time, never at call time. */
        ModuleExportMarshaling decoded;
        return decode_module_export_marshaling(static_cast<std::uint32_t>(value), decoded);
      }

      void add_error(ModuleManifestErrorCode code, const YAML::Mark& mark, std::string field, std::string message) {
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

      void reject_custom_tags(const YAML::Node& node, const std::string& field, std::size_t depth) {
        if (!node.IsDefined()) return;
        if (depth > max_module_manifest_depth || inspected_nodes_ >= max_module_manifest_nodes) {
          if (!node_limit_reported_) {
            add_error(ModuleManifestErrorCode::LimitExceeded, node.Mark(), field, "YAML nesting or node count exceeds the manifest limit");
            node_limit_reported_ = true;
          }
          return;
        }
        ++inspected_nodes_;
        if (!is_implicit_tag(node.Tag())) {
          add_error(ModuleManifestErrorCode::UnsupportedTag, node.Mark(), field, "custom YAML tags are not supported");
        }
        if (node.IsMap()) {
          for (const auto& pair : node) {
            std::string key = "<key>";
            if (pair.first.IsScalar()) key = pair.first.Scalar();
            const auto nested = child_field(field, key);
            if (!is_implicit_tag(pair.first.Tag())) {
              add_error(ModuleManifestErrorCode::UnsupportedTag, pair.first.Mark(), nested, "custom YAML tags are not supported on mapping keys");
            }
            reject_custom_tags(pair.second, nested, depth + 1);
          }
        } else if (node.IsSequence()) {
          for (std::size_t index = 0; index < node.size(); ++index) {
            reject_custom_tags(node[index], field + '[' + std::to_string(index) + ']', depth + 1);
          }
        }
      }

      std::vector<MapEntry> read_map(const YAML::Node& node, const std::string& field, std::initializer_list<std::string_view> allowed) {
        if (!node.IsMap()) {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), field, "expected a mapping");
          return {};
        }
        if (node.size() > max_module_manifest_entries) {
          add_error(ModuleManifestErrorCode::LimitExceeded, node.Mark(), field, "mapping exceeds the 1024-entry manifest limit");
          return {};
        }
        std::vector<MapEntry> entries;
        std::set<std::string> seen;
        for (const auto& pair : node) {
          if (!pair.first.IsScalar()) {
            add_error(ModuleManifestErrorCode::WrongType, pair.first.Mark(), field, "mapping keys must be strings");
            continue;
          }
          std::string key = pair.first.Scalar();
          const auto nested = child_field(field, key);
          if (!seen.insert(key).second) {
            add_error(ModuleManifestErrorCode::DuplicateKey, pair.first.Mark(), nested, "mapping keys must be unique");
          }
          if (!is_allowed(key, allowed)) {
            add_error(ModuleManifestErrorCode::UnknownField, pair.first.Mark(), nested, "field is not part of module manifest schema version 2");
          }
          entries.push_back({std::move(key), pair.second});
        }
        return entries;
      }

      static const YAML::Node* find_entry(const std::vector<MapEntry>& entries, std::string_view key) {
        const auto found = std::ranges::find_if(entries, [=](const MapEntry& entry) { return entry.key == key; });
        return found == entries.end() ? nullptr : &found->value;
      }

      const YAML::Node* require_entry(const std::vector<MapEntry>& entries, std::string_view key, const std::string& field, const YAML::Mark& mark) {
        if (const auto* node = find_entry(entries, key)) return node;
        add_error(ModuleManifestErrorCode::MissingField, mark, child_field(field, key), "required field is missing");
        return nullptr;
      }

      bool read_string(const YAML::Node& node, const std::string& field, std::string& output) {
        if (!node.IsScalar()) {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), field, "expected a string");
          return false;
        }
        output = node.Scalar();
        return true;
      }

      bool read_unsigned(const YAML::Node& node, const std::string& field, std::uint64_t& output) {
        if (!node.IsScalar() || node.Tag() != "?") {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), field, "expected an unquoted unsigned integer");
          return false;
        }
        const auto value = node.Scalar();
        const auto converted = std::from_chars(value.data(), value.data() + value.size(), output);
        if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size()) {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), field, "expected an unsigned integer");
          return false;
        }
        return true;
      }

      bool read_bool(const YAML::Node& node, const std::string& field, bool& output) {
        if (!node.IsScalar() || node.Tag() != "?") {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), field, "expected an unquoted boolean");
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
        add_error(ModuleManifestErrorCode::InvalidValue, node.Mark(), field, "expected true or false");
        return false;
      }

      void parse_root(const YAML::Node& root) {
        const auto entries = read_map(root, {}, {"schema", "api", "abi", "entry", "threads", "shared-memory", "toolchain", "exports", "payloads"});
        const auto* schema = require_entry(entries, "schema", {}, root.Mark());
        const auto* api = require_entry(entries, "api", {}, root.Mark());
        const auto* abi = require_entry(entries, "abi", {}, root.Mark());
        const auto* entry = require_entry(entries, "entry", {}, root.Mark());
        const auto* exports = require_entry(entries, "exports", {}, root.Mark());

        std::uint64_t schema_value = 0;
        if (schema && read_unsigned(*schema, "schema", schema_value)) {
          if (schema_value != module_manifest_schema_version) {
            add_error(ModuleManifestErrorCode::UnsupportedSchema, schema->Mark(), "schema", "only module manifest schema version 2 is supported");
          } else {
            manifest_.schema = static_cast<std::uint32_t>(schema_value);
          }
        }
        std::uint64_t api_version = 0;
        if (api && read_unsigned(*api, "api", api_version)) {
          if (api_version == 0 || api_version > UINT32_MAX) {
            add_error(ModuleManifestErrorCode::InvalidValue, api->Mark(), "api", "module API version must be positive and fit in 32 bits");
          } else {
            manifest_.api_version = static_cast<std::uint32_t>(api_version);
          }
        }
        std::uint64_t abi_version = 0;
        if (abi && read_unsigned(*abi, "abi", abi_version)) {
          if (abi_version == 0 || abi_version > UINT32_MAX) {
            add_error(ModuleManifestErrorCode::InvalidValue, abi->Mark(), "abi", "module ABI version must be positive and fit in 32 bits");
          } else {
            manifest_.abi_version = static_cast<std::uint32_t>(abi_version);
          }
        }
        if (entry && read_string(*entry, "entry", manifest_.entry) && manifest_.entry != module_entry_symbol_v1) {
          add_error(ModuleManifestErrorCode::InvalidValue, entry->Mark(), "entry", "expected the mobagen_module_entry_v1 entry symbol");
        }
        if (const auto* threads = find_entry(entries, "threads")) {
          std::string value;
          if (read_string(*threads, "threads", value)) {
            if (value == "none") {
              manifest_.threads = ModuleThreadsPolicy::None;
            } else if (value == "managed") {
              manifest_.threads = ModuleThreadsPolicy::Managed;
            } else {
              add_error(ModuleManifestErrorCode::InvalidValue, threads->Mark(), "threads", "expected none or managed");
            }
          }
        }
        if (const auto* shared = find_entry(entries, "shared-memory")) {
          read_bool(*shared, "shared-memory", manifest_.shared_memory);
        }
        if (const auto* toolchain = find_entry(entries, "toolchain")) parse_toolchain(*toolchain);
        if (exports) parse_exports(*exports);
        if (const auto* payloads = find_entry(entries, "payloads")) parse_payloads(*payloads);
      }

      void parse_toolchain(const YAML::Node& node) {
        const auto entries = read_map(node, "toolchain", {"producer", "version"});
        const auto* producer = require_entry(entries, "producer", "toolchain", node.Mark());
        const auto* version = require_entry(entries, "version", "toolchain", node.Mark());
        if (!producer && !version) return;
        ModuleManifestToolchain parsed;
        if (producer) read_string(*producer, "toolchain.producer", parsed.producer);
        if (version) read_string(*version, "toolchain.version", parsed.version);
        if (parsed.producer.empty() || parsed.version.empty()) {
          add_error(ModuleManifestErrorCode::InvalidValue, node.Mark(), "toolchain", "toolchain producer and version must be non-empty");
        }
        manifest_.toolchain = std::move(parsed);
      }

      void parse_exports(const YAML::Node& node) {
        if (!node.IsSequence()) {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), "exports", "expected a sequence");
          return;
        }
        if (node.size() > max_module_manifest_entries) {
          add_error(ModuleManifestErrorCode::LimitExceeded, node.Mark(), "exports", "export count exceeds the 1024-entry manifest limit");
          return;
        }
        std::set<std::string> seen;
        for (std::size_t index = 0; index < node.size(); ++index) {
          const auto field = "exports[" + std::to_string(index) + ']';
          const auto entries = read_map(node[index], field, {"name", "signature", "return", "params"});
          const auto* name = require_entry(entries, "name", field, node[index].Mark());
          const auto* signature = require_entry(entries, "signature", field, node[index].Mark());
          const auto* return_type = find_entry(entries, "return");
          const auto* params = find_entry(entries, "params");
          ModuleManifestExport parsed;
          if (name && read_string(*name, field + ".name", parsed.name) && parsed.name.empty()) {
            add_error(ModuleManifestErrorCode::InvalidValue, name->Mark(), field + ".name", "export name must be non-empty");
          }
          std::uint64_t signature_id = 0;
          if (signature && read_unsigned(*signature, field + ".signature", signature_id)) {
            if (!is_valid_signature_id(signature_id)) {
              add_error(ModuleManifestErrorCode::InvalidValue, signature->Mark(), field + ".signature",
                        "signature id is not a valid module ABI v1 signature id");
            } else {
              parsed.signature_id = static_cast<std::uint32_t>(signature_id);
              ModuleExportMarshaling decoded;
              static_cast<void>(decode_module_export_marshaling(parsed.signature_id, decoded));
              parsed.marshaling = decoded;
            }
          }
          if (!parsed.name.empty() && !seen.insert(parsed.name).second) {
            add_error(ModuleManifestErrorCode::DuplicateEntry, node[index].Mark(), field, "export names must be unique");
          }
          /* The decoded marshaling descriptor (todo 10) is DERIVED data: when
           * present it must agree with signature_id exactly, otherwise
           * serialization would not be canonical. Mismatch = parse error. */
          if (parsed.marshaling.has_value() && (return_type != nullptr || params != nullptr)) {
            const auto& expected = *parsed.marshaling;
            if (return_type != nullptr) {
              std::string value;
              if (read_string(*return_type, field + ".return", value)) {
                const auto type = marshaling_type_from_name(value);
                if (!type.has_value()) {
                  add_error(ModuleManifestErrorCode::InvalidValue, return_type->Mark(), field + ".return", "unknown marshaling type code");
                } else if (*type != expected.return_type) {
                  add_error(ModuleManifestErrorCode::InvalidValue, return_type->Mark(), field + ".return",
                            "return type disagrees with the signature id");
                }
              }
            }
            if (params != nullptr) {
              if (!params->IsSequence()) {
                add_error(ModuleManifestErrorCode::WrongType, params->Mark(), field + ".params", "expected a sequence of marshaling type codes");
              } else if (params->size() != expected.param_count) {
                add_error(ModuleManifestErrorCode::InvalidValue, params->Mark(), field + ".params",
                          "parameter count disagrees with the signature id");
              } else {
                for (std::size_t param = 0; param < params->size(); ++param) {
                  const auto param_field = field + ".params[" + std::to_string(param) + ']';
                  std::string value;
                  if (params->operator[](param).IsScalar() && read_string(params->operator[](param), param_field, value)) {
                    const auto type = marshaling_type_from_name(value);
                    if (!type.has_value()) {
                      add_error(ModuleManifestErrorCode::InvalidValue, params->operator[](param).Mark(), param_field,
                                "unknown marshaling type code");
                    } else if (*type != expected.params[param]) {
                      add_error(ModuleManifestErrorCode::InvalidValue, params->operator[](param).Mark(), param_field,
                                "parameter type disagrees with the signature id");
                    }
                  }
                }
              }
            }
          }
          manifest_.exports.push_back(std::move(parsed));
        }
      }

      void parse_payloads(const YAML::Node& node) {
        if (!node.IsSequence()) {
          add_error(ModuleManifestErrorCode::WrongType, node.Mark(), "payloads", "expected a sequence");
          return;
        }
        if (node.size() > max_module_manifest_entries) {
          add_error(ModuleManifestErrorCode::LimitExceeded, node.Mark(), "payloads", "payload count exceeds the 1024-entry manifest limit");
          return;
        }
        std::set<std::string> seen;
        for (std::size_t index = 0; index < node.size(); ++index) {
          const auto field = "payloads[" + std::to_string(index) + ']';
          const auto entries = read_map(node[index], field, {"file", "hash", "size"});
          const auto* file = require_entry(entries, "file", field, node[index].Mark());
          const auto* hash = require_entry(entries, "hash", field, node[index].Mark());
          const auto* size = require_entry(entries, "size", field, node[index].Mark());
          ModuleManifestPayload parsed;
          if (file && read_string(*file, field + ".file", parsed.filename) && parsed.filename != module_wasm_payload_filename
              && parsed.filename != module_aot_payload_filename) {
            add_error(ModuleManifestErrorCode::InvalidValue, file->Mark(), field + ".file", "payload filename must be plugin.wasm or plugin.aot");
          }
          if (hash && read_string(*hash, field + ".hash", parsed.hash) && !is_sha256(parsed.hash)) {
            add_error(ModuleManifestErrorCode::InvalidHash, hash->Mark(), field + ".hash", "payload hash is not canonical SHA-256");
          }
          std::uint64_t size_value = 0;
          if (size && read_unsigned(*size, field + ".size", size_value)) {
            parsed.size = size_value;
          }
          if (!parsed.filename.empty() && !seen.insert(parsed.filename).second) {
            add_error(ModuleManifestErrorCode::DuplicateEntry, node[index].Mark(), field, "payload filenames must be unique");
          }
          manifest_.payloads.push_back(std::move(parsed));
        }
      }

      ModuleManifestParseResult finish() {
        ModuleManifestParseResult result{.errors = std::move(errors_)};
        if (result.errors.empty()) result.manifest = std::move(manifest_);
        return result;
      }

      std::string_view source_;
      std::string source_path_;
      ModuleManifest manifest_;
      std::vector<ModuleManifestError> errors_;
      std::size_t inspected_nodes_{};
      bool node_limit_reported_{};
    };

  }  // namespace

  ModuleManifestParseResult parse_module_manifest(std::string_view source, std::string_view source_path) {
    return ModuleManifestParser{source, source_path}.parse();
  }

  std::string serialize_module_manifest(const ModuleManifest& manifest) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "schema: " << manifest.schema << '\n';
    output << "api: " << manifest.api_version << '\n';
    output << "abi: " << manifest.abi_version << '\n';
    output << "entry: " << manifest.entry << '\n';
    output << "threads: " << module_threads_policy_name(manifest.threads) << '\n';
    output << "shared-memory: " << (manifest.shared_memory ? "true" : "false") << '\n';
    if (manifest.toolchain.has_value()) {
      output << "toolchain:\n";
      output << "  producer: " << manifest.toolchain->producer << '\n';
      output << "  version: " << manifest.toolchain->version << '\n';
    }
    if (manifest.exports.empty()) {
      output << "exports: []\n";
    } else {
      output << "exports:\n";
      for (const auto& entry : manifest.exports) {
        output << "  - name: " << entry.name << '\n';
        output << "    signature: " << entry.signature_id << '\n';
        /* Derived marshaling descriptor (todo 10): decoded from the signature
         * id so the dispatchers can walk the manifest mechanically. Always
         * emitted for valid ids; byte-stable because the decode is pure. */
        ModuleExportMarshaling decoded;
        if (decode_module_export_marshaling(entry.signature_id, decoded)) {
          std::string_view return_name;
          if (marshaling_type_name(decoded.return_type, return_name)) {
            output << "    return: " << return_name << '\n';
            output << "    params: [";
            for (std::uint32_t param = 0; param < decoded.param_count; ++param) {
              std::string_view param_name;
              if (marshaling_type_name(decoded.params[param], param_name)) {
                if (param != 0) output << ", ";
                output << param_name;
              }
            }
            output << "]\n";
          }
        }
      }
    }
    if (manifest.payloads.empty()) {
      output << "payloads: []\n";
    } else {
      output << "payloads:\n";
      for (const auto& payload : manifest.payloads) {
        output << "  - file: " << payload.filename << '\n';
        output << "    hash: " << payload.hash << '\n';
        output << "    size: " << payload.size << '\n';
      }
    }
    return std::move(output).str();
  }

  std::string module_manifest_signature(const ModuleManifest& manifest) {
    std::ostringstream canonical;
    canonical.imbue(std::locale::classic());
    for (const auto& entry : manifest.exports) {
      canonical << entry.name << ':' << entry.signature_id << '\n';
    }
    const auto text = std::move(canonical).str();
    const auto digest = assets::sha256(std::as_bytes(std::span{text.data(), text.size()}));
    return digest.has_value() ? assets::to_string(*digest) : std::string{};
  }

}  // namespace mobagen::modules
