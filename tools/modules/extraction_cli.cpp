#include "extraction_cli.hpp"

#include "modules/module_manifest.hpp"
#include <mobagen/module/module_abi.h>

#include "assets/asset_id.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mobagen::modules::cli {
  namespace {

    constexpr std::string_view kUsage
        = "usage:\n"
          "  MobagenModuleManifest manifest [options] <plugin.wasm> <module.manifest>\n"
          "options:\n"
          "  --aot <plugin.aot>            hash the .aot payload into the manifest\n"
          "  --toolchain-producer <name>   toolchain.producer stamp (e.g. wamrc)\n"
          "  --toolchain-version <ver>     toolchain.version stamp (e.g. 2.4.5)\n"
          "  --shared-memory               stamp shared-memory: true (todo 18; for\n"
          "                                guests whose producer omits the\n"
          "                                target_features section, e.g. emcc)\n";

    constexpr std::size_t kTableHeaderBytes = 16;
    constexpr std::size_t kTableEntryBytes = 16; /* MobagenModuleExportEntryV1 on wasm32 */
    constexpr std::uint32_t kMaxTableEntries = 1024;
    constexpr std::size_t kMaxNameBytes = 4096;

    /* ---- minimal wasm binary reader (export, global, and data sections) ---- */

    class WasmReader {
    public:
      explicit WasmReader(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}

      /* Positions at the payload of section `id`; false when absent or malformed. */
      [[nodiscard]] bool seek_section(unsigned char id) { return seek_section_after(id, 8); }

      /* Same, but starts scanning after `from` (in bytes) — lets callers walk
       * past earlier custom sections to a later same-id one. */
      [[nodiscard]] bool seek_section_after(unsigned char id, std::size_t from) {
        if (bytes_.size() < 8) return false;
        static constexpr unsigned char kHeader[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
        if (std::memcmp(bytes_.data(), kHeader, 8) != 0) return false;
        offset_ = from;
        limit_ = bytes_.size();
        while (offset_ < bytes_.size()) {
          const auto section_id = read_byte();
          const auto length = read_varuint();
          if (length > bytes_.size() - offset_) return false;
          const auto payload = offset_;
          if (section_id == id) {
            limit_ = payload + length;
            return true;
          }
          offset_ = payload + length;
        }
        return false;
      }

      [[nodiscard]] std::uint32_t read_varuint() {
        std::uint32_t value = 0;
        for (unsigned shift = 0; shift < 32 && offset_ < limit_; shift += 7) {
          const auto byte = bytes_[offset_++];
          value |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
          if ((byte & 0x80) == 0) break;
        }
        return value;
      }

      [[nodiscard]] std::int64_t read_varint() {
        std::int64_t value = 0;
        unsigned shift = 0;
        for (; shift < 64 && offset_ < limit_; shift += 7) {
          const auto byte = bytes_[offset_++];
          value |= static_cast<std::int64_t>(byte & 0x7F) << shift;
          if ((byte & 0x80) == 0) {
            if (shift + 7 < 64 && (byte & 0x40) != 0) value |= static_cast<std::int64_t>(-1) << (shift + 7);
            break;
          }
        }
        return value;
      }

      [[nodiscard]] std::string read_name() {
        const auto length = read_varuint();
        if (length > remaining()) {
          offset_ = limit_;
          return {};
        }
        std::string name(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return name;
      }

      [[nodiscard]] unsigned char read_byte() { return offset_ < limit_ ? bytes_[offset_++] : 0; }

      void skip(std::size_t count) { offset_ = std::min(offset_ + count, limit_); }

      [[nodiscard]] std::size_t offset() const { return offset_; }

      [[nodiscard]] std::size_t limit() const { return limit_; }

      [[nodiscard]] std::size_t remaining() const { return limit_ - std::min(offset_, limit_); }

    private:
      const std::vector<unsigned char>& bytes_;
      std::size_t offset_{};
      std::size_t limit_{};
    };

    struct DataSegment {
      std::uint32_t offset{};
      std::uint32_t size{};
      std::size_t bytes_at{};
    };

    /* Constant-expression scanner; yields the last i32.const value. */
    [[nodiscard]] std::optional<std::uint32_t> scan_init_expr(WasmReader& reader) {
      std::optional<std::uint32_t> constant;
      for (;;) {
        const auto opcode = reader.read_byte();
        switch (opcode) {
          case 0x0B: /* end */
            return constant;
          case 0x41: /* i32.const */
            constant = static_cast<std::uint32_t>(reader.read_varint());
            break;
          case 0x42: /* i64.const */
          case 0x23: /* global.get */
          case 0xD2: /* ref.func */
            static_cast<void>(reader.read_varint());
            break;
          case 0x43: /* f32.const */
            reader.skip(4);
            break;
          case 0x44: /* f64.const */
            reader.skip(8);
            break;
          case 0xD0: /* ref.null */
            reader.skip(1);
            break;
          default:
            return std::nullopt;
        }
      }
    }

    struct ExtractedTable {
      std::uint32_t abi_version{};
      std::vector<ModuleManifestExport> exports;
    };

    [[nodiscard]] bool valid_signature_id(std::uint32_t id) {
      /* Mirrors mobagen_module_signature_decode without pointer outputs. */
      if ((id >> 24) != MOBAGEN_MODULE_SIG_MAGIC) return false;
      if ((id & 0x3U) != 0) return false;
      return ((id >> 20) & 0xFU) <= MOBAGEN_MODULE_MAX_EXPORT_PARAMS;
    }

    /*
     * Linear-memory view: active data segments over an implicitly zeroed
     * memory. wasm-opt legally trims trailing NUL bytes off the final data
     * segment (zero-initialized memory), so string reads may continue past a
     * segment end into implicit zeros.
     */
    class LinearMemory {
    public:
      LinearMemory(const std::vector<unsigned char>& binary, std::vector<DataSegment> segments) : binary_(binary), segments_(std::move(segments)) {
        std::uint32_t end = 0;
        for (const auto& segment : segments_) {
          if (segment.offset == UINT32_MAX) continue; /* passive: unknown offset */
          end = std::max(end, segment.offset + segment.size);
        }
        extent_ = end;
      }

      [[nodiscard]] const unsigned char* bytes(std::uint32_t address, std::uint32_t size) const {
        for (const auto& segment : segments_) {
          if (address >= segment.offset && size <= segment.size && address - segment.offset <= segment.size - size) {
            return binary_.data() + segment.bytes_at + (address - segment.offset);
          }
        }
        /* Reads falling off the end of the last segment read as zero-init memory. */
        if (address >= extent_ && static_cast<std::uint64_t>(address) + size <= static_cast<std::uint64_t>(extent_) + kTailSlack) {
          return kZero;
        }
        return nullptr;
      }

      /* Byte-wise read across segment boundaries and inter-segment holes
       * (todo 18): emcc's linker places the published-global storage at the
       * very END of a data segment, so the 4-byte indirection spans real
       * bytes, implicit zeros, or both — linear memory is zero-initialized
       * everywhere below the data extent. */
      [[nodiscard]] bool read_u32_spanning(std::uint32_t address, std::uint32_t& value) const {
        value = 0;
        for (std::uint32_t index = 0; index < 4; ++index) {
          const auto at = address + index;
          if (at >= extent_ + kTailSlack) return false;
          const auto* byte = bytes(at, 1);
          if (byte == nullptr) {
            if (at >= extent_) return false;
            continue;
          }
          value |= static_cast<std::uint32_t>(*byte) << (8U * index);
        }
        return true;
      }

    private:
      static constexpr std::uint32_t kTailSlack = kMaxNameBytes + 1;
      static constexpr unsigned char kZero[1] = {0};

      const std::vector<unsigned char>& binary_;
      std::vector<DataSegment> segments_;
      std::uint32_t extent_{};
    };

    [[nodiscard]] std::uint32_t read_u32(const unsigned char* bytes) {
      std::uint32_t value = 0;
      std::memcpy(&value, bytes, sizeof(value));
      return value;
    }

    [[nodiscard]] bool looks_like_table_header(const LinearMemory& memory, std::uint32_t address) {
      const auto* header = memory.bytes(address, kTableHeaderBytes);
      return header != nullptr && read_u32(header) == MOBAGEN_MODULE_TABLE_V1;
    }

    [[nodiscard]] std::optional<std::uint32_t> find_table_export(WasmReader& reader) {
      const auto count = reader.read_varuint();
      std::optional<std::uint32_t> found;
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto name = reader.read_name();
        const auto kind = reader.read_byte();
        const auto item = reader.read_varuint();
        if (name == MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1 && kind == 0x03) found = item;
      }
      return found;
    }

    [[nodiscard]] std::optional<std::uint32_t> read_global_init(WasmReader& reader, std::uint32_t global_index) {
      const auto count = reader.read_varuint();
      if (global_index >= count) return std::nullopt;
      for (std::uint32_t index = 0; index <= global_index; ++index) {
        static_cast<void>(reader.read_byte()); /* content type */
        static_cast<void>(reader.read_byte()); /* mutability */
        const auto constant = scan_init_expr(reader);
        if (index == global_index) return constant;
      }
      return std::nullopt;
    }

    [[nodiscard]] std::vector<DataSegment> read_data_segments(WasmReader& reader) {
      std::vector<DataSegment> segments;
      const auto count = reader.read_varuint();
      for (std::uint32_t index = 0; index < count; ++index) {
        const auto flags = reader.read_varuint();
        if (flags != 0) {
          /* Passive segments never carry the annotation table at a statically
           * known linear offset (they are copied by runtime memory.init) — but
           * their bytes may still contain the table itself (todo 18: shared
           * emcc guests emit every segment passive). Record them with an
           * unknown offset; the magic-scan fallback reads their bytes only. */
          const auto size = reader.read_varuint();
          if (size > reader.remaining()) break;
          segments.push_back({UINT32_MAX, size, reader.offset()});
          reader.skip(size);
          continue;
        }
        const auto constant = scan_init_expr(reader);
        if (!constant.has_value()) break;
        const auto size = reader.read_varuint();
        if (size > reader.remaining()) break;
        segments.push_back({*constant, size, reader.offset()});
        reader.skip(size);
      }
      return segments;
    }

    /*
     * Shared-memory capability detection (todo 18): wasm producers targeting
     * shared memory emit a `target_features` custom section listing features
     * as (disposition, name) pairs; `+atomics` marks a guest compiled with
     * -matomics (wasi-sdk clang). emscripten emits no target_features section,
     * so an emcc-built guest stamps false here — the web packaging path
     * supplies the flag itself. Modules may carry several custom sections
     * (name, producers, ...) before target_features, hence the full walk.
     */
    [[nodiscard]] bool declares_atomics_target_feature(const std::vector<unsigned char>& binary) {
      WasmReader reader{binary};
      std::size_t from = 8;
      while (reader.seek_section_after(0, from)) {
        const auto custom_name = reader.read_name();
        if (custom_name == "target_features") {
          const auto count = reader.read_varuint();
          for (std::uint32_t index = 0; index < count && index <= kMaxTableEntries; ++index) {
            const auto disposition = static_cast<char>(reader.read_byte());
            const auto feature = reader.read_name();
            if (disposition == '+' && feature == "atomics") return true;
          }
          return false;
        }
        from = reader.limit();
      }
      return false;
    }

    /*
     * The exported wasm global resolves either directly to the annotation table
     * (linker folded the address) or to the four storage bytes of the published
     * const global, whose value is the table offset. Follow one indirection at
     * most, validated by the table kind magic.
     */
    [[nodiscard]] std::optional<std::uint32_t> resolve_table_address(const LinearMemory& memory, std::uint32_t global_value) {
      if (looks_like_table_header(memory, global_value)) return global_value;
      std::uint32_t indirect = 0;
      if (!memory.read_u32_spanning(global_value, indirect)) return std::nullopt;
      if (looks_like_table_header(memory, indirect)) return indirect;
      return std::nullopt;
    }

    [[nodiscard]] std::optional<ExtractedTable> extract_annotation_table(const std::vector<unsigned char>& binary, std::string& failure) {
      std::optional<std::uint32_t> table_global;
      {
        WasmReader reader{binary};
        if (!reader.seek_section(7)) {
          failure = "wasm binary has no export section";
          return std::nullopt;
        }
        table_global = find_table_export(reader);
        if (!table_global.has_value()) {
          failure = std::string{"annotation export table missing: no exported global "} + MOBAGEN_MODULE_TABLE_EXPORT_SYMBOL_V1;
          return std::nullopt;
        }
      }

      std::optional<std::uint32_t> global_value;
      {
        WasmReader reader{binary};
        if (reader.seek_section(6)) {
          global_value = read_global_init(reader, *table_global);
        } else {
          /* export sections may reference only imported globals */
          failure = "annotation export table global is not defined in this module";
          return std::nullopt;
        }
      }
      if (!global_value.has_value()) {
        failure = "annotation export table address is not a constant";
        return std::nullopt;
      }

      std::vector<DataSegment> segments;
      {
        WasmReader reader{binary};
        if (reader.seek_section(11)) segments = read_data_segments(reader);
      }
      const LinearMemory memory{binary, std::move(segments)};

      const auto table_address = resolve_table_address(memory, *global_value);
      if (!table_address.has_value()) {
        failure = "annotation export table is not present in wasm linear memory";
        return std::nullopt;
      }
      const auto* header = memory.bytes(*table_address, kTableHeaderBytes);
      std::uint32_t abi_version = read_u32(header + 4);
      std::uint32_t entry_count = read_u32(header + 8);
      if (abi_version != MOBAGEN_MODULE_ABI_VERSION) {
        failure = "annotation table abi version mismatch";
        return std::nullopt;
      }
      if (entry_count > kMaxTableEntries) {
        failure = "annotation table entry count exceeds the manifest limit";
        return std::nullopt;
      }

      ExtractedTable table;
      table.abi_version = abi_version;
      table.exports.reserve(entry_count);
      const std::uint32_t first_entry = *table_address + kTableHeaderBytes;
      for (std::uint32_t index = 0; index < entry_count; ++index) {
        const auto entry_address = first_entry + index * kTableEntryBytes;
        const auto* entry = memory.bytes(entry_address, kTableEntryBytes);
        if (entry == nullptr) {
          failure = "annotation table entry " + std::to_string(index) + " is not present in wasm linear memory";
          return std::nullopt;
        }
        const auto name_address = read_u32(entry);
        const auto signature_id = read_u32(entry + 8);
        if (!valid_signature_id(signature_id)) {
          failure = "annotation table entry " + std::to_string(index) + " carries an invalid signature id";
          return std::nullopt;
        }
        std::string name;
        for (std::uint32_t cursor = name_address; name.size() <= kMaxNameBytes; ++cursor) {
          const auto* byte = memory.bytes(cursor, 1);
          if (byte == nullptr) {
            failure = "annotation table entry " + std::to_string(index) + " name is not NUL-terminated in wasm linear memory";
            return std::nullopt;
          }
          if (*byte == 0) break;
          name.push_back(static_cast<char>(*byte));
        }
        if (name.empty() || name.size() > kMaxNameBytes) {
          failure = "annotation table entry " + std::to_string(index) + " has an invalid name";
          return std::nullopt;
        }
        table.exports.push_back({std::move(name), signature_id});
      }
      return table;
    }

    [[nodiscard]] std::optional<std::vector<unsigned char>> read_file_bytes(const std::filesystem::path& path) {
      std::error_code system_error;
      const auto size = std::filesystem::file_size(path, system_error);
      if (system_error) return std::nullopt;
      std::ifstream stream{path, std::ios::binary};
      std::vector<unsigned char> bytes(size);
      if (size > 0 && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) return std::nullopt;
      return bytes;
    }

    int manifest(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
      /* AOT stamping (todo 20's CMake stage). With no options the output is
       * byte-identical to the pre-todo-20 tool (golden-manifest stability).
       * --shared-memory (todo 18) asserts the guest was compiled
       * shared-memory-capable — the flag set is known to the build system
       * (emcc emits no target_features section to detect). */
      std::optional<std::string> aot_path;
      std::optional<std::string> toolchain_producer;
      std::optional<std::string> toolchain_version;
      bool shared_memory_override = false;
      std::size_t index = 0;
      for (; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--aot" || argument == "--toolchain-producer" || argument == "--toolchain-version") {
          if (index + 1 >= arguments.size()) {
            error << kUsage << "option " << argument << " requires a value\n";
            return 2;
          }
          if (argument == "--aot") {
            aot_path = arguments[++index];
          } else if (argument == "--toolchain-producer") {
            toolchain_producer = arguments[++index];
          } else {
            toolchain_version = arguments[++index];
          }
          continue;
        }
        if (argument == "--shared-memory") {
          shared_memory_override = true;
          continue;
        }
        break;
      }
      const auto positional = arguments.subspan(index);
      if (positional.size() != 2) {
        error << kUsage;
        return 2;
      }
      if (toolchain_producer.has_value() != toolchain_version.has_value()) {
        error << "manifest failed: --toolchain-producer and --toolchain-version must be given together\n";
        return 2;
      }

      const auto wasm_path = std::filesystem::path{std::string{positional[0]}};
      const auto binary = read_file_bytes(wasm_path);
      if (!binary.has_value()) {
        error << "manifest failed: wasm payload could not be read\n";
        return 3;
      }

      std::string failure;
      const auto table = extract_annotation_table(*binary, failure);
      if (!table.has_value()) {
        error << "manifest failed: " << failure << '\n';
        return 3;
      }

      const auto digest = assets::sha256(std::as_bytes(std::span{reinterpret_cast<const std::byte*>(binary->data()), binary->size()}));
      if (!digest.has_value()) {
        error << "manifest failed: wasm payload could not be hashed\n";
        return 3;
      }

      ModuleManifest manifest;
      manifest.schema = module_manifest_schema_version;
      manifest.api_version = 1;
      manifest.abi_version = table->abi_version;
      manifest.entry = std::string{module_entry_symbol_v1};
      manifest.threads = ModuleThreadsPolicy::None;
      manifest.shared_memory = shared_memory_override || declares_atomics_target_feature(*binary);
      manifest.exports = table->exports;
      manifest.payloads.push_back({std::string{module_wasm_payload_filename}, assets::to_string(*digest), binary->size()});
      if (toolchain_producer.has_value()) {
        manifest.toolchain = ModuleManifestToolchain{*toolchain_producer, *toolchain_version};
      }
      if (aot_path.has_value()) {
        const auto aot = read_file_bytes(std::filesystem::path{*aot_path});
        if (!aot.has_value()) {
          error << "manifest failed: aot payload could not be read (" << *aot_path << ")\n";
          return 3;
        }
        const auto aot_digest = assets::sha256(std::as_bytes(std::span{reinterpret_cast<const std::byte*>(aot->data()), aot->size()}));
        if (!aot_digest.has_value()) {
          error << "manifest failed: aot payload could not be hashed\n";
          return 3;
        }
        manifest.payloads.push_back({std::string{module_aot_payload_filename}, assets::to_string(*aot_digest), aot->size()});
      }

      const auto serialized = serialize_module_manifest(manifest);
      const auto parse_back = parse_module_manifest(serialized);
      if (!parse_back.ok()) {
        error << "manifest failed: generated manifest does not reparse";
        if (!parse_back.errors.empty()) error << ": " << parse_back.errors.front().message;
        error << '\n';
        return 3;
      }

      std::ofstream out{std::filesystem::path{std::string{positional[1]}}, std::ios::binary | std::ios::trunc};
      if (!out.good()) {
        error << "manifest failed: cannot write " << positional[1] << '\n';
        return 3;
      }
      out << serialized;
      out.flush();
      if (!out.good()) {
        error << "manifest failed: cannot write " << positional[1] << '\n';
        return 3;
      }
      output << "manifest\t" << positional[1] << '\t' << table->exports.size() << " exports\n";
      output << "signature\t" << module_manifest_signature(manifest) << '\n';
      return 0;
    }

  }  // namespace

  int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) {
    try {
      if (!arguments.empty() && arguments[0] == "manifest") {
        return manifest(arguments.subspan(1), output, error);
      }
      if (arguments.size() == 1 && (arguments[0] == "help" || arguments[0] == "--help")) {
        output << kUsage;
        return 0;
      }
      error << kUsage;
      return 2;
    } catch (const std::exception& exception) {
      error << "module manifest command failed: " << exception.what() << '\n';
      return 3;
    } catch (...) {
      error << "module manifest command failed: unknown error\n";
      return 3;
    }
  }

}  // namespace mobagen::modules::cli
