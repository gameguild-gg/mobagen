#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
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

  inline constexpr std::uint32_t module_abi_max_export_params = 5;

  /*
   * Marshaling type codes — the decoded view of the module ABI v1 signature-id
   * type codes (sdk/include/mobagen/module/module_abi.h MOBAGEN_MODULE_T_*).
   * The numbering is identical on purpose: the descriptor carries no
   * vocabulary beyond what the signature table already defines.
   */
  enum class ModuleMarshalingType : std::uint8_t {
    Void = 0,
    I32 = 1,
    I64 = 2,
    F32 = 3,
    F64 = 4,
    Ptr = 5,
    Span = 6,
  };

  /*
   * The marshaling DESCRIPTOR for one annotated export (todo 10): pure data
   * decoded from the signature-id. The generic dispatchers — the browser
   * backend's typed invoke / host-import seam and the WAMR native shims —
   * walk this mechanically: i32/i64/f32/f64 pass through, ptr resolves to a
   * bounds-checked offset, span to a bounds-checked (offset,size) pair
   * against the guest linear memory.
   */
  struct ModuleExportMarshaling {
    ModuleMarshalingType return_type{ModuleMarshalingType::Void};
    std::uint32_t param_count{};
    std::array<ModuleMarshalingType, module_abi_max_export_params> params{}; /* first param_count entries valid */
  };

  /*
   * THE shared decode helper (one place; header-only so every consumer —
   * manifest validation, the extraction tool, both dispatchers — includes it
   * without link dependencies). False = the id is not decodable. That is a
   * load-time contract: manifests carrying it are rejected at parse, and a
   * dispatcher that still receives one fails loudly at the call boundary.
   */
  [[nodiscard]] inline bool decode_module_export_marshaling(std::uint32_t signature_id, ModuleExportMarshaling& out) noexcept {
    if ((signature_id >> 24) != 0x4DU || (signature_id & 0x3U) != 0) return false;
    const auto count = (signature_id >> 20) & 0xFU;
    if (count > out.params.size()) return false;
    const auto return_code = (signature_id >> 17) & 0x7U;
    if (return_code > static_cast<std::uint32_t>(ModuleMarshalingType::Span)) return false;
    out.return_type = static_cast<ModuleMarshalingType>(return_code);
    out.param_count = count;
    for (std::uint32_t index = 0; index < count; ++index) {
      const auto code = (signature_id >> (14U - 3U * index)) & 0x7U;
      if (code > static_cast<std::uint32_t>(ModuleMarshalingType::Span)) return false;
      out.params[index] = static_cast<ModuleMarshalingType>(code);
    }
    return true;
  }

  /*
   * Cells (u32 words) one descriptor's argument list occupies — the wasm
   * boundary convention shared by both dispatchers: i32/f32/ptr/span take
   * one cell (a span cell names the guest-memory (offset,size) pair), i64
   * and f64 take two.
   */
  [[nodiscard]] inline std::uint32_t module_marshaling_cell_count(const ModuleExportMarshaling& marshaling) noexcept {
    std::uint32_t cells = 0;
    for (std::uint32_t index = 0; index < marshaling.param_count; ++index) {
      const auto type = marshaling.params[index];
      cells += type == ModuleMarshalingType::I64 || type == ModuleMarshalingType::F64 ? 2U : 1U;
    }
    return cells;
  }

  /*
   * Validates a cell list against a decoded descriptor and the guest linear
   * memory size BEFORE any call: exact cell count, ptr offsets inside memory
   * (0 is the ABI null offset and passes), span cells pointing at a readable
   * 8-byte (offset,size) pair (the wasm32 calling convention passes the
   * by-value MobagenModuleSpan32 struct by hidden reference — one i32
   * argument naming where the pair lives). Pure.
   */
  [[nodiscard]] inline bool module_marshaling_cells_in_bounds(const ModuleExportMarshaling& marshaling, std::span<const std::uint32_t> cells,
                                                              std::size_t memory_bytes) noexcept {
    if (cells.size() != module_marshaling_cell_count(marshaling)) return false;
    std::uint32_t cell = 0;
    for (std::uint32_t index = 0; index < marshaling.param_count; ++index) {
      switch (marshaling.params[index]) {
        case ModuleMarshalingType::I64:
        case ModuleMarshalingType::F64:
          cell += 2U;
          break;
        case ModuleMarshalingType::Ptr: {
          const auto offset = cells[cell++];
          if (offset != 0 && static_cast<std::size_t>(offset) >= memory_bytes) return false;
          break;
        }
        case ModuleMarshalingType::Span: {
          const auto span_ptr = cells[cell++];
          if (static_cast<std::size_t>(span_ptr) + 8U > memory_bytes) return false;
          break;
        }
        default:
          ++cell;
          break;
      }
    }
    return true;
  }

  /* Reads the (offset,size) pair a span cell points at. Caller guarantees the
   * 8 readable bytes (module_marshaling_cells_in_bounds). */
  [[nodiscard]] inline bool read_module_span_cell(std::span<const std::byte> memory, std::uint32_t span_ptr, std::uint32_t& offset,
                                                  std::uint32_t& size) noexcept {
    if (static_cast<std::size_t>(span_ptr) + 8U > memory.size()) return false;
    const auto* raw = reinterpret_cast<const unsigned char*>(memory.data() + span_ptr);
    offset = static_cast<std::uint32_t>(raw[0]) | (static_cast<std::uint32_t>(raw[1]) << 8U) | (static_cast<std::uint32_t>(raw[2]) << 16U)
             | (static_cast<std::uint32_t>(raw[3]) << 24U);
    size = static_cast<std::uint32_t>(raw[4]) | (static_cast<std::uint32_t>(raw[5]) << 8U) | (static_cast<std::uint32_t>(raw[6]) << 16U)
           | (static_cast<std::uint32_t>(raw[7]) << 24U);
    return static_cast<std::size_t>(offset) + static_cast<std::size_t>(size) <= memory.size();
  }

  enum class ModuleThreadsPolicy : std::uint8_t { None, Managed };

  struct ModuleManifestExport {
    std::string name;
    std::uint32_t signature_id{};

    /*
     * Decoded marshaling descriptor (todo 10) — pure data derived from
     * signature_id; written by the serializer, validated by the parser, and
     * walked mechanically by the dispatchers. Optional on input: parse keeps
     * it in sync with signature_id when absent; a disagreement is a parse
     * error so serialization stays canonical (byte-stable).
     */
    std::optional<ModuleExportMarshaling> marshaling;
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
