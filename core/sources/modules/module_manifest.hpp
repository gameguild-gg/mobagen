#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mobagen::modules {

  /* module.manifest describes one compiled wasm module payload (.plugin v2 packages). */
  inline constexpr std::uint32_t module_manifest_schema_version = 2;
  inline constexpr std::size_t max_module_manifest_bytes = 1024 * 1024;

  inline constexpr std::string_view module_manifest_filename = "module.manifest";
  inline constexpr std::string_view module_wasm_payload_filename = "plugin.wasm";
  inline constexpr std::string_view module_aot_payload_filename = "plugin.aot";
  inline constexpr std::string_view module_entry_symbol_v1 = "mobagen_module_entry_v1";

  enum class ModuleThreadsPolicy : std::uint8_t { None, Managed };

  struct ModuleManifestExport {
    std::string name;
    std::uint32_t signature_id{};
  };

  struct ModuleManifestToolchain {
    std::string producer;
    std::string version;
  };

  struct ModuleManifestPayload {
    std::string filename; /* plugin.wasm | plugin.aot */
    std::string hash;     /* sha256:<64 lowercase hex> */
    std::uint64_t size{};
  };

  struct ModuleManifest {
    std::uint32_t schema{module_manifest_schema_version};
    std::uint32_t api_version{};
    std::uint32_t abi_version{};
    std::string entry; /* entry symbol, e.g. mobagen_module_entry_v1 */
    ModuleThreadsPolicy threads{ModuleThreadsPolicy::None};
    bool shared_memory{false}; /* shared-memory-safe capability; distinct from threads */
    std::optional<ModuleManifestToolchain> toolchain;
    std::vector<ModuleManifestExport> exports;
    std::vector<ModuleManifestPayload> payloads; /* per-file SHA-256 hashes */
  };

  enum class ModuleManifestErrorCode : std::uint8_t {
    Syntax,
    DuplicateKey,
    UnsupportedTag,
    UnknownField,
    MissingField,
    WrongType,
    UnsupportedSchema,
    InvalidValue,
    InvalidHash,
    LimitExceeded,
    DuplicateEntry,
  };

  struct ModuleManifestError {
    ModuleManifestErrorCode code{};
    std::string source_path;
    std::size_t line{};
    std::size_t column{};
    std::string field;
    std::string message;
  };

  struct ModuleManifestParseResult {
    std::optional<ModuleManifest> manifest;
    std::vector<ModuleManifestError> errors;

    [[nodiscard]] bool ok() const noexcept { return manifest.has_value() && errors.empty(); }
  };

  [[nodiscard]] ModuleManifestParseResult parse_module_manifest(std::string_view source, std::string_view source_path = "module.manifest");

  /* Canonical deterministic serialization (byte-stable for identical manifests). */
  [[nodiscard]] std::string serialize_module_manifest(const ModuleManifest& manifest);

  /* Stable digest (sha256:<hex>) over the canonical export table; used by the lockfile signature field. */
  [[nodiscard]] std::string module_manifest_signature(const ModuleManifest& manifest);

  [[nodiscard]] constexpr std::string_view module_threads_policy_name(ModuleThreadsPolicy policy) noexcept {
    return policy == ModuleThreadsPolicy::Managed ? "managed" : "none";
  }

}  // namespace mobagen::modules
